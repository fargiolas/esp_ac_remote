#!/usr/bin/python

data = ["10000000010010010111111110001111110011001010100000001111",
        "10000000010001010111111110001111110000101010100000001111",
        "10000000010001010111111110001111110001001010100000001111",
        "10000000010011010111111110001111110000001010001000001111",
        "10000000010000010111111110001111110001111010001000001111",
        "10000000010010110111010110001111110001001000100000001111",
        "10000000010010010111111110001111110001011010100000001111",
        "10000000010010010111111110001111110010101010001000001111",
        "10000000010000110111010110001111110001011000100000001111"
]

for bitstr in data:
    chunks = [int(x[::-1],2) for x in map(''.join, (zip(*[iter(bitstr)]*8)))]

    print("{} {}".format(bitstr, " ".join(["{:02X}".format(c) for c in chunks])))

    print("checksum: {} {:01X}".format(bitstr[12:16], chunks[1]>>4))

    partial = "".join([bitstr[:12], bitstr[16:]])
    print partial
    onecount = partial.count('1')
    zerocount = partial.count('0')
    print("ones: {} {:02X}".format(onecount, onecount))
    print("zeros: {} {:02X}".format(zerocount, zerocount))

    chksum = ((28-onecount)<<4)|0x02
    print("{:02X}".format(chksum))
    print bin(chksum)
    print

