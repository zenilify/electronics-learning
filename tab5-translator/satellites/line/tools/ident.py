import sys,re
for f in sys.argv[1:]:
    d=open(f,'rb').read(); print('###',f,len(d))
    print('nonFF MB', round(sum(1 for i in range(0,len(d),4096) if d[i:i+4096].strip(b'\xff'))*4096/1e6,2))
    pt=d[0x8000:0x8C00]
    for i in range(0,len(pt),32):
        e=pt[i:i+32]
        if e[:2]!=b'\xaa\x50': break
        print(' part',e[12:28].rstrip(b'\0').decode(), hex(int.from_bytes(e[4:8],'little')), hex(int.from_bytes(e[8:12],'little')))
    for m in re.finditer(b'\x32\x54\xcd\xab',d):
        o=m.start(); g=lambda a,b:d[o+a:o+b].rstrip(b'\0').decode('latin1')
        print(' app@',hex(o),'name=',g(48,80),'ver=',g(16,48),g(96,112),g(80,96),'idf=',g(112,144))
    for k in [b'Cardputer',b'cardputer',b'TCA8418',b'StampS3',b'AtomS3',b'ATOMS3',b'M5Unified',b'Bruce',b'UIFlow',b'uiflow',b'M5Launcher',b'QRCode',b'MFRC522',b'UserDemo',b'ST7789',b'GC9107',b'BMI270',b'ES8311',b'm5stack-',b'arduino-lib-builder']:
        n=d.count(k)
        if n: print('  ',k.decode(),n)
