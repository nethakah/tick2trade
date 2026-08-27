'''
sudo -E python3 run.py
'''

from pynq import allocate, Overlay
import numpy, time, os

CORE_PERIOD_NS = 4.167 # 1/240 MHz

# matching localparams in csr module
REG_CONTROL = 0x00
REG_TRIGGER_PRICE = 0x04
REG_ORDER_SHARES = 0x08
REG_SPREAD_MAX = 0x0C
REG_SIZE_MIN = 0x10
REG_STOCK_LOCATE = 0x14
REG_BEST_BID_PRICE = 0x20
REG_BEST_BID_SHARES = 0x24
REG_BEST_ASK_PRICE = 0x28
REG_BEST_ASK_SHARES = 0x2C
REG_SPREAD = 0x30
REG_FIRE_COUNT = 0x34
REG_PACKET_COUNT = 0x38
REG_GAP_COUNT = 0x3C
REG_MISS_COUNT = 0x40
REG_OVERFLOW_COUNT = 0x44
REG_LEVEL_COLLISION = 0x48
REG_FIRE_LATENCY = 0x4C
REG_LATENCY_MIN = 0x50
REG_LATENCY_MAX = 0x54
REG_INGEST_LATENCY = 0x58

# program the fpga
ol = Overlay('tick2trade.bit')
csr = ol.tick2trade
dma = ol.axi_dma

# cfg so we dont fire on stale params
csr.write(REG_STOCK_LOCATE, 1)
csr.write(REG_TRIGGER_PRICE, 1230500)
csr.write(REG_ORDER_SHARES, 500)
csr.write(REG_SPREAD_MAX, 1000)
csr.write(REG_SIZE_MIN, 100)
csr.write(REG_CONTROL, 0x3)

# 'rb' = read+binary
f = open('itch_data.bin', 'rb')
data = f.read()
f.close()

print(f'Loaded {len(data)} B from itch_data.bin')

# gen_itch writes expected.txt so check if its there first
expected = {}
if os.path.exists('expected.txt'):
    for line in open('expected.txt'):
        key, value = line.split()
        expected[key] = int(value)

def split_packets(data):
    packets = []
    pos = 0
    while pos < len(data):
        packet_start = pos
        msg_count = int.from_bytes(data[pos+18:pos+20], 'big')
        pos += 20

        for i in range(msg_count):
            msg_len = int.from_bytes(data[pos:pos+2], 'big')
            pos += 2 + msg_len

        packets.append(data[packet_start:pos])
    return packets

packets = split_packets(data)
largest = max(len(p) for p in packets)
print(f'{len(packets)} packets; largest packet is {largest} B')

buf = allocate(shape=(largest,), dtype=numpy.uint8)

start = time.perf_counter()
for p in packets:
    # copy into front of buffer + transfer ensuring tlast lands on packet boundary
    buf[0:len(p)] = numpy.frombuffer(p, dtype=numpy.uint8)
    dma.sendchannel.transfer(buf[0:len(p)])
    dma.sendchannel.wait()
elapsed = time.perf_counter() - start

# extra time since final bytes might still be crossing CDC/draining
time.sleep(0.05)

if len(data) > 10000:
    ms = elapsed * 1000
    mbps = len(data) / elapsed / 1000000
    print()
    print(f'DMA transfer: {len(data)} B in {ms:.2f}ms ({mbps:.1f}MB/s)')

print()
print(f'packet_count = {csr.read(REG_PACKET_COUNT)}')
print(f'gap_count = {csr.read(REG_GAP_COUNT)}')
print(f'best_bid_price = {csr.read(REG_BEST_BID_PRICE)}')
print(f'best_ask_price = {csr.read(REG_BEST_ASK_PRICE)}')
print(f'best_bid_shares = {csr.read(REG_BEST_BID_SHARES)}')
print(f'best_ask_shares = {csr.read(REG_BEST_ASK_SHARES)}')
print(f'spread = {csr.read(REG_SPREAD)}')
print(f'fire_count = {csr.read(REG_FIRE_COUNT)}')
print(f'miss_count = {csr.read(REG_MISS_COUNT)}')
print(f'overflow_count = {csr.read(REG_OVERFLOW_COUNT)}')
print(f'level_collision = {csr.read(REG_LEVEL_COLLISION)}')

# hardware timestamps measured in fabric and given over axi-lite
cycles = csr.read(REG_FIRE_LATENCY)
print()
print(f'Decision latency: {cycles} cycles ({cycles * CORE_PERIOD_NS:.1f} ns)')

if len(expected) > 0:
    actual = dict()
    actual['best_bid_price'] = csr.read(REG_BEST_BID_PRICE)
    actual['best_bid_shares'] = csr.read(REG_BEST_BID_SHARES)
    actual['best_ask_price'] = csr.read(REG_BEST_ASK_PRICE)
    actual['best_ask_shares'] = csr.read(REG_BEST_ASK_SHARES)
    actual['spread'] = csr.read(REG_SPREAD)
    actual['miss_count'] = csr.read(REG_MISS_COUNT)
    actual['packet_count'] = csr.read(REG_PACKET_COUNT)

    failures = 0
    print()
    for k in expected:
        fpga = actual[k]
        model = expected[k]
        if fpga != model:
            print(f'Mismatch on key = {k}; hardware = {fpga}; model = {model}')
            failures += 1

    overflow = csr.read(REG_OVERFLOW_COUNT)
    if overflow != 0:
        print(f'ERROR: overflow_count = {overflow}; lower MAX_LIVE_ORDERS to fix this')
        failures += 1
    gaps = csr.read(REG_GAP_COUNT)
    if gaps != 0:
        print(f'ERROR: gap_count = {gaps}; deframer lost the sequence')
        failures += 1

    if failures == 0:
        print('PASSED!')
    else:
        print(f'FAILED ({failures} mismatches)')

# free the block we were using
buf.freebuffer()