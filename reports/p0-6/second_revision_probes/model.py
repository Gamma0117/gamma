"""Small specification models; these are not P0-6 production implementations."""

from collections import deque
from fractions import Fraction
import math
import random


class Server:
    def __init__(self):
        self.highest = 0
        self.neutralized = 0
        self.pending = deque()
        self.priming = True
        self.waited = 0
        self.applied = []
        self.dropped = 0
        self.previous_ack = 0

    def input(self, seq, intent="W"):
        if seq <= self.highest:
            return
        self.highest = seq
        self.pending.append((seq, "neutral" if seq <= self.neutralized else intent))

    def neutralize(self, through):
        self.neutralized = max(self.neutralized, through)
        self.pending = deque((seq, "neutral" if seq <= through else intent)
                             for seq, intent in self.pending)

    def cap(self):
        while len(self.pending) > 4:
            self.pending.popleft()
            self.dropped += 1

    def ack(self):
        return self.highest if not self.pending else self.pending[0][0] - 1

    def tick(self):
        self.cap()
        selected = None
        if not self.pending:
            self.priming = True
            self.waited = 0
        elif self.priming and len(self.pending) < 2 and self.waited < 2:
            self.waited += 1
        else:
            selected = self.pending.popleft()
            self.priming = False
            assert selected[0] > self.previous_ack
            assert selected[0] not in [seq for seq, _ in self.applied]
            self.applied.append(selected)
        ack = self.ack()
        assert ack >= self.previous_ack
        assert all(seq > ack for seq, _ in self.pending)
        self.previous_ack = ack
        return selected, ack


for arrivals, expected in [([1, 2, 4, 3], [1, 3, 4]),
                           ([1, 2, 2], [1, 2]),
                           (list(range(1, 41)) + [40], [37, 38, 39, 40])]:
    server = Server()
    for seq in arrivals:
        server.input(seq)
    acknowledgements = []
    while server.pending:
        selected, ack = server.tick()
        if selected:
            acknowledgements.append(ack)
    assert acknowledgements == expected
    print("ack_case", {"arrivals": arrivals if len(arrivals) < 10 else "1..40,40",
                       "ack": acknowledgements})

for intent in ["W", "jump", "neutral"]:
    server = Server()
    server.input(1, intent)
    timeline = [server.tick() for _ in range(5)]
    assert [selected for selected, _ in timeline] == [None, None, (1, intent), None, None]
    assert timeline[2][1] == 1
print("singleton", "W/jump/neutral all consumed and acknowledged on tick 3")

server = Server()
server.neutralize(10)
server.input(10, "jump")
assert server.tick()[0] is None
assert server.tick()[0] is None
assert server.tick()[0] == (10, "neutral")
print("neutralize_before_input", "input 10 remains neutral")

# Record saturation pauses numbering; FIFO messages then produce a resolved prefix.
records = deque()
mailbox = []
last_sent = 0
pause_entries = 0
paused = False
for _ in range(70):
    if len(records) == 40:
        if not paused:
            pause_entries += 1
            paused = True
        continue
    last_sent += 1
    records.append(last_sent)
    mailbox.append(last_sent)
assert last_sent == len(records) == 40 and pause_entries == 1
server = Server()
for seq in mailbox:
    server.input(seq)
server.cap()
assert server.ack() == 36
records = deque(seq for seq in records if seq > server.ack())
assert list(records) == [37, 38, 39, 40]
selected, ack = server.tick()
records = deque(seq for seq in records if seq > ack)
assert records[0] == ack + 1
last_sent += 1
records.append(last_sent)
assert list(records) == [38, 39, 40, 41]
print("record_pause", {"last_sent": last_sent, "records": list(records), "pause_entries": pause_entries})

# Resync stops creating new sequence numbers until the saved boundary is acknowledged.
server = Server()
server.input(101)
server.input(102)
server.previous_ack = 100
boundary = 102
ticks = 0
while server.ack() < boundary:
    _, ack = server.tick()
    ticks += 1
assert ack == boundary and not server.pending
records = deque([boundary + 1])
assert records[0] == ack + 1
server.input(records[0])
_, ack = server.tick()
assert ack == 103
print("resync_barrier", {"drain_ticks": ticks, "first_new_input": 103, "ack": ack})

# Explore arrivals, duplicates, gaps, neutralization, and pauses with a fixed seed.
rng = random.Random(67026)
server = Server()
published = 0
for _ in range(20000):
    for _ in range(rng.randrange(0, 8)):
        if rng.randrange(4) == 0:
            server.neutralize(rng.randrange(0, server.highest + 5))
        else:
            seq = max(1, server.highest + rng.randrange(-3, 4))
            server.input(seq)
    selected, published = server.tick()
    assert all(seq > published for seq, _ in server.pending)
print("random_server_interleavings", "20,000 ticks passed")

# Same-rate 20Hz clocks, different render rates/phases and both same-time orderings.
cases = 0
for fps in [30, 60, 144]:
    for phase_ms in range(50):
        for server_first in [False, True]:
            server = Server()
            next_client_deadline = Fraction(0)
            frame = 0
            server_time = Fraction(phase_ms, 1000)
            last_sent = 0
            began = False
            duration = Fraction(10)
            while server_time < duration:
                frame_time = Fraction(frame, fps)
                is_server = (server_time < frame_time or
                             (server_time == frame_time and server_first))
                if is_server:
                    selected, _ = server.tick()
                    if began:
                        assert selected is not None, (fps, phase_ms, server_first, server_time)
                    began |= selected is not None
                    server_time += Fraction(1, 20)
                else:
                    if frame_time >= next_client_deadline:
                        due = int((frame_time - next_client_deadline) / Fraction(1, 20)) + 1
                        assert due <= 3
                        next_client_deadline += due * Fraction(1, 20)
                        for _ in range(due):
                            last_sent += 1
                            server.input(last_sent)
                    frame += 1
            assert server.dropped == 0
            while server.pending:
                server.tick()
            assert [seq for seq, _ in server.applied] == list(range(1, last_sent + 1))
            cases += 1
print("render_rate_phases", {"cases": cases, "rates": [30, 60, 144],
                             "dropped": 0, "starvation_after_first_application": 0})

# The minimum body dimensions leave nonempty cell ranges at positive/negative boundaries.
epsilon = 1e-7
for center in [0, 16, -16, 30_000_000, -30_000_000]:
    for width in [0.1, 4]:
        first = math.floor(center - width / 2 + epsilon)
        last = math.ceil(center + width / 2 - epsilon) - 1
        assert first <= last
print("minimum_body_ranges", "nonempty at 0, +/-16, +/-30,000,000")

# A frame-local block flag survives recapture and suppresses old/new tap edges.
for old_latch in [False, True]:
    released = False
    captured = True
    previous_accepting = True
    latch = old_latch
    captured = False  # Escape.
    released = True
    captured = True  # Same-frame click.
    frame_blocked = released
    if frame_blocked:
        latch = False
    if previous_accepting and not frame_blocked and captured:
        latch |= True  # Space edge in this poll.
    assert captured and released and not latch
print("same_frame_recapture", "capture remains true; old/new Space tap is discarded")
