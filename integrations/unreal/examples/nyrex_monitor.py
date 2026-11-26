import argparse
import json
import socket
import struct
import sys
import time

SUPPORTED_SCHEMA = 1
HELLO_TIMEOUT = 5.0
PING_TIMEOUT = 2.0
MAX_PING_ID = 16777215


def pad(data):
    return data + b"\0" * (4 - len(data) % 4)


def read_string(data, offset):
    end = data.index(b"\0", offset)
    text = data[offset:end].decode("utf-8", "replace")
    return text, (end + 4) & ~3


def decode_message(data):
    address, offset = read_string(data, 0)
    if offset >= len(data):
        return address, []
    tags, offset = read_string(data, offset)
    values = []
    for tag in tags[1:]:
        if tag == "i":
            values.append(struct.unpack_from(">i", data, offset)[0])
            offset += 4
        elif tag == "f":
            values.append(struct.unpack_from(">f", data, offset)[0])
            offset += 4
        elif tag == "s":
            text, offset = read_string(data, offset)
            values.append(text)
        elif tag in "TFN":
            values.append(tag == "T" if tag != "N" else None)
        else:
            break
    return address, values


def decode_packet(data, depth=0):
    if depth > 4 or not data:
        return []
    if data.startswith(b"#bundle\0"):
        messages = []
        offset = 16
        while offset + 4 <= len(data):
            size = struct.unpack_from(">i", data, offset)[0]
            offset += 4
            if size <= 0 or offset + size > len(data):
                break
            messages.extend(decode_packet(data[offset:offset + size], depth + 1))
            offset += size
        return messages
    try:
        return [decode_message(data)]
    except (ValueError, struct.error):
        return []


def encode_ping(ping_id):
    return pad(b"/nyrex/ping") + pad(b",i") + struct.pack(">i", ping_id)


def percentile(sorted_values, fraction):
    if not sorted_values:
        return 0.0
    index = max(0, min(len(sorted_values) - 1, int(-(-fraction * len(sorted_values) // 1)) - 1))
    return sorted_values[index]


def main():
    parser = argparse.ArgumentParser(description="Listens to NyrexVJ OSC output, tracks the connection and measures the round trip of /nyrex/ping.")
    parser.add_argument("--listen-port", type=int, default=9011)
    parser.add_argument("--control-host", default="127.0.0.1")
    parser.add_argument("--control-port", type=int, default=9010)
    parser.add_argument("--interval", type=float, default=0.5)
    parser.add_argument("--duration", type=float, default=0.0)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--require-pongs", type=int, default=0)
    parser.add_argument("--stop-on", choices=["never", "ended", "lost"], default="never")
    args = parser.parse_args()

    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        listener.bind(("0.0.0.0", args.listen_port))
    except OSError as error:
        print("cannot listen on UDP port %d: %s" % (args.listen_port, error), file=sys.stderr)
        return 2
    listener.settimeout(0.01)
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = (args.control_host, args.control_port)

    started = time.perf_counter()
    last_report = started
    last_ping = None
    next_id = 1
    pending = {}
    rtts = []
    sent = 0
    lost = 0
    ignored = 0
    packets = 0
    hellos = 0
    last_hello = None
    connected = False
    connections = 0
    losses = 0
    schema = None
    version = None
    input_port = None
    events = {"beat": 0, "onset": 0, "audio": 0, "clock": 0, "spectrum": 0}
    state = "waiting"

    try:
        while True:
            now = time.perf_counter()
            if args.duration > 0 and now - started >= args.duration:
                break
            try:
                data, source = listener.recvfrom(65535)
                received = time.perf_counter()
            except (socket.timeout, OSError):
                data = None
                received = now
            if data:
                packets += 1
                for address, values in decode_packet(data):
                    if address == "/nyrex/out/hello":
                        hellos += 1
                        last_hello = received
                        if len(values) == 4 and isinstance(values[1], int) and isinstance(values[2], str) and isinstance(values[3], int):
                            schema = values[1]
                            version = values[2]
                            input_port = values[3]
                        else:
                            schema = None
                        if schema == SUPPORTED_SCHEMA:
                            state = "connected"
                        else:
                            state = "incompatible"
                    elif address == "/nyrex/out/bye":
                        last_hello = None
                        state = "ended"
                    elif address == "/nyrex/out/pong" and len(values) == 1 and isinstance(values[0], int):
                        sent_at = pending.pop(values[0], None)
                        if sent_at is None or received - sent_at > PING_TIMEOUT:
                            ignored += 1
                        else:
                            rtts.append((received - sent_at) * 1000.0)
                    else:
                        for key in events:
                            if address == "/nyrex/out/" + key:
                                events[key] += 1
            now = time.perf_counter()
            alive = state == "connected" and last_hello is not None and now - last_hello < HELLO_TIMEOUT
            if alive and not connected:
                connections += 1
                if not args.quiet and not args.json:
                    print("%6.2fs connected  NyrexVJ %s schema %s input port %s" % (now - started, version, schema, input_port))
            if not alive and connected:
                losses += 1 if state != "ended" else 0
                if state == "connected":
                    state = "lost"
                if not args.quiet and not args.json:
                    print("%6.2fs disconnected (%s)" % (now - started, state))
            connected = alive
            if args.stop_on == state:
                break
            for ping_id in [key for key, value in pending.items() if now - value > PING_TIMEOUT]:
                del pending[ping_id]
                lost += 1
            if connected and (last_ping is None or now - last_ping >= args.interval):
                ping_id = next_id
                next_id = 1 if next_id >= MAX_PING_ID else next_id + 1
                pending[ping_id] = time.perf_counter()
                last_ping = now
                sent += 1
                try:
                    sender.sendto(encode_ping(ping_id), target)
                except OSError:
                    pass
            if not args.quiet and not args.json and now - last_report >= 2.0 and rtts:
                last_report = now
                window = sorted(rtts[-64:])
                print("%6.2fs rtt last %.2f ms  avg %.2f ms  min %.2f  max %.2f  p95 %.2f  (%d pongs, %d lost)" % (now - started, rtts[-1], sum(window) / len(window), window[0], window[-1], percentile(window, 0.95), len(rtts), lost))
    except KeyboardInterrupt:
        pass

    window = sorted(rtts[-64:])
    summary = {
        "state": state if not connected else "connected",
        "connected": connected,
        "connections": connections,
        "losses": losses,
        "packets": packets,
        "hellos": hellos,
        "version": version,
        "schema": schema,
        "inputPort": input_port,
        "pingsSent": sent,
        "pongs": len(rtts),
        "pingsLost": lost,
        "ignored": ignored,
        "events": events,
        "rttMs": {
            "last": round(rtts[-1], 3) if rtts else None,
            "average": round(sum(window) / len(window), 3) if window else None,
            "min": round(window[0], 3) if window else None,
            "max": round(window[-1], 3) if window else None,
            "p95": round(percentile(window, 0.95), 3) if window else None,
        },
    }
    if args.json:
        print(json.dumps(summary))
    elif not args.quiet:
        print("summary: " + json.dumps(summary))
    if args.require_pongs > 0 and len(rtts) < args.require_pongs:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
