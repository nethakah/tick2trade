'''
sudo -E python3 run.py
'''

from pynq import allocate, Overlay
import numpy, time

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

# return numpy array in physically contiguous mem
buf = allocate(shape=(len(data),), dtype=numpy.uint8)

# buf[:] so we assign into existing array instead of rebinding
buf[:] = numpy.frombuffer(data, dtype=numpy.uint8)

# write buffer's physical addr/len into DMA regs and start it
dma.sendchannel.transfer(buf)
# poll DMA status reg until done
dma.sendchannel.wait()

# extra time since final bytes might still be crossing CDC/draining
time.sleep(0.01)

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

# free the block we were using
buf.freebuffer()