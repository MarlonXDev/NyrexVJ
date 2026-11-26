import os
import subprocess
import sys
import tempfile

import numpy as np

WIDTH = 320
HEIGHT = 180
CLIPS = {
    "test_flow_30.mp4": "steady",
    "test_flow_60.mp4": "steady60",
    "test_flow_cut.mp4": "cut",
    "test_flow_vfr.mp4": "vfr",
}

YY, XX = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float32)
XX += 0.5
YY += 0.5


def scene(x, y, channel):
    c = float(channel)
    v = np.full_like(x, 0.5)
    v += 0.18 * np.sin(0.21 * x + 0.13 * y + c)
    v += 0.15 * np.sin(0.07 * x - 0.19 * y + 2.0 * c)
    v += 0.10 * np.sin(0.33 * x + 0.02 * y)
    v += 0.10 * np.sin(0.05 * x + 0.41 * y + 1.0)
    v += 0.08 * np.sin(0.50 * x - 0.30 * y)
    v += 0.25 * np.exp(-((x - 90.0) ** 2 + (y - 60.0) ** 2) / 260.0)
    v -= 0.20 * np.exp(-((x - 220.0) ** 2 + (y - 110.0) ** 2) / 420.0)
    return np.clip(v, 0.0, 1.0)


def frame_at(tau, variant=0):
    out = np.zeros((HEIGHT, WIDTH, 3), dtype=np.float32)
    if variant == 0:
        disc_x = 60.0 + 7.0 * tau
        disc_y = 70.0
        rect_x = 270.0 - 6.0 * tau
        rect_y = 120.0 - 1.5 * tau
        in_disc = (XX - disc_x) ** 2 + (YY - disc_y) ** 2 < 30.0 * 30.0
        in_rect = (np.abs(XX - rect_x) < 28.0) & (np.abs(YY - rect_y) < 22.0)
        for c in range(3):
            background = scene(XX - 2.0 * tau, YY - 0.5 * tau, c) * 0.8 + 0.1
            disc = scene(XX - disc_x + 400.0, YY - disc_y + 300.0, c)
            rect = scene(XX - rect_x + 700.0, YY - rect_y + 50.0, c)
            out[:, :, c] = np.where(in_disc, disc, np.where(in_rect, rect, background))
    else:
        disc_x = 250.0 - 5.0 * tau
        disc_y = 130.0
        in_disc = (XX - disc_x) ** 2 + (YY - disc_y) ** 2 < 35.0 * 35.0
        for c in range(3):
            background = 1.0 - (scene(YY * 1.3 + 40.0 + 3.0 * tau, XX * 0.9 + 90.0, (c + 1) % 3) * 0.8 + 0.1)
            disc = scene(XX * 0.6 - disc_x + 900.0, YY * 0.6 - disc_y + 100.0, c)
            out[:, :, c] = np.where(in_disc, disc, background)
    return (np.clip(out, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def write_frames(directory, frames):
    for index, frame in enumerate(frames):
        header = b"P6\n%d %d\n255\n" % (WIDTH, HEIGHT)
        with open(os.path.join(directory, "f%04d.ppm" % index), "wb") as handle:
            handle.write(header + frame.tobytes())


def encode_constant(directory, rate, target):
    subprocess.run(
        [
            "ffmpeg", "-y", "-loglevel", "error", "-framerate", str(rate), "-i", os.path.join(directory, "f%04d.ppm"),
            "-c:v", "libx264", "-preset", "slow", "-crf", "10", "-g", "15", "-bf", "0", "-pix_fmt", "yuv420p", "-movflags", "+faststart", target,
        ],
        check=True,
    )


def encode_variable(directory, times, target):
    terms = "+".join("eq(N\\,%d)*%.6f" % (index, value) for index, value in enumerate(times))
    subprocess.run(
        [
            "ffmpeg", "-y", "-loglevel", "error", "-framerate", "1000", "-i", os.path.join(directory, "f%04d.ppm"),
            "-vf", "setpts=(%s)/TB" % terms, "-fps_mode", "vfr", "-enc_time_base:v", "1/1000",
            "-c:v", "libx264", "-preset", "slow", "-crf", "10", "-g", "15", "-bf", "0", "-pix_fmt", "yuv420p", "-video_track_timescale", "1000", target,
        ],
        check=True,
    )


def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "data")
    for name, kind in CLIPS.items():
        target = os.path.join(target_dir, name)
        with tempfile.TemporaryDirectory() as directory:
            if kind == "steady":
                write_frames(directory, [frame_at(float(i)) for i in range(30)])
                encode_constant(directory, 30, target)
            elif kind == "steady60":
                write_frames(directory, [frame_at(i / 2.0) for i in range(60)])
                encode_constant(directory, 60, target)
            elif kind == "cut":
                write_frames(directory, [frame_at(float(i), 0 if i < 15 else 1) for i in range(30)])
                encode_constant(directory, 30, target)
            else:
                durations = [1 / 30.0] * 6 + [1 / 20.0] * 4 + [1 / 30.0] * 4 + [0.2] + [1 / 30.0] * 8 + [1 / 24.0] * 4
                times = np.concatenate([[0.0], np.cumsum(durations)[:-1]])
                write_frames(directory, [frame_at(t * 30.0) for t in times])
                encode_variable(directory, [float(t) for t in times], target)
        print(name, os.path.getsize(target))


main()
