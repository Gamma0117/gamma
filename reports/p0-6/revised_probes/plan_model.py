from collections import deque
import math

# The proposed refill gate on a finite one-input stream.
pending = deque([1])
primed = False
ack = 0
applied = []
for tick in range(1, 11):
    if not primed and len(pending) >= 2:
        primed = True
    if primed and pending:
        seq = pending.popleft()
        applied.append(seq)
        ack = max(ack, seq)
        if not pending:
            primed = False
print('single_input_after_10_ticks', {'pending': list(pending), 'ack': ack, 'applied': applied})

# Demonstrate why max(retired seq) is not a cumulative acknowledgement.
pending = deque()
received_max = 0
retired_max = 0
for seq in [1, 2, 4, 3]:
    if seq <= received_max:
        retired_max = max(retired_max, seq)
    else:
        received_max = seq
        pending.append(seq)
selected = pending.popleft()
retired_max = max(retired_max, selected)
print('literal_max_retired_ack', {'applied': selected, 'ack': retired_max, 'still_pending': list(pending),
                                 'pending_acknowledged': [seq for seq in pending if seq <= retired_max]})

# Entered resync with input 101/102 still pending; continue sending one new input per server tick.
server_pending = deque([101, 102])
client_records = []
last_sent = 102
ack = 100
for tick in range(1, 6):
    last_sent += 1
    client_records.append(last_sent)
    server_pending.append(last_sent)
    ack = server_pending.popleft()
    gap = bool(client_records) and client_records[0] != ack + 1
    print('resync_continued_input', {'tick': tick, 'last_sent': last_sent, 'ack': ack,
                                   'first_record': client_records[0] if client_records else None, 'resync': gap})
    if gap:
        client_records.clear()

width = 1e-8
center = 16.0
epsilon = 1e-7
first = math.floor(center - width / 2 + epsilon)
last = math.ceil(center + width / 2 - epsilon) - 1
print('valid_tiny_width_query', {'width': width, 'first_cell': first, 'last_cell': last, 'empty': first > last})
