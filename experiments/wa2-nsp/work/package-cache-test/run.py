from pathlib import Path
import hashlib,subprocess,tempfile
base=Path(__file__).resolve().parent
blobs=[bytes(range(256))*n+b'end' for n in (1,256,8192)]
s='struct wa2_package_asset {const char *dos,*relative,*package; unsigned long long size;unsigned char sha[32];};\nstatic const struct wa2_package_asset wa2_package_assets[]={\n'
for i,b in enumerate(blobs):s+='{"","drive_c/WA2/a%d","wapkg:/a%d",%d,{%s}},\n'%(i,i,len(b),','.join(str(x) for x in hashlib.sha256(b).digest()))
s+='};\n#define WA2_PACKAGE_COUNT 3\n#define WA2_PACKAGE_ID "'+('a'*64)+'"\n'
(base/'wine/nx_package_assets.h').write_text(s)
exe=base/'cache-test';subprocess.run(['gcc','-std=c11','-I'+str(base),str(base/'test.c'),'/lib/x86_64-linux-gnu/libcrypto.so.3','-o',str(exe)],check=True)
with tempfile.TemporaryDirectory(prefix='wa2-cache-') as temp:
 p=Path(temp);(p/'test-root/drive_c/WA2').mkdir(parents=True);(p/'wapkg:').mkdir()
 for i,b in enumerate(blobs):(p/f'wapkg:/a{i}').write_bytes(b)
 def run(ok,mode):
  r=subprocess.run([str(exe)],cwd=p,stdout=subprocess.DEVNULL);assert (r.returncode==0)==ok
  log=(p/'test-root/package-mount.log').read_text();assert 'mode='+mode in log;return log
 run(True,'full');assert (p/'test-root/package-ready.txt').read_bytes()==b'a'*64
 log=run(True,'cached');assert log.count('sha256=cached')==3
 (p/'test-root/drive_c/WA2/a1').write_bytes(b'x');run(False,'cached');(p/'test-root/drive_c/WA2/a1').unlink()
 (p/'wapkg:/a1').write_bytes(b'x');run(False,'cached');(p/'wapkg:/a1').write_bytes(blobs[1])
 (p/'test-root/package-ready.txt').write_text('partial');run(True,'full')
 (p/'wapkg:/a0').write_bytes(b'X'+blobs[0][1:]);(p/'test-root/verify-package.flag').touch();run(False,'full');assert (p/'test-root/verify-package.flag').exists()
 (p/'wapkg:/a0').write_bytes(blobs[0]);run(True,'full');assert not (p/'test-root/verify-package.flag').exists()
 (p/'test-root/package-ready.txt').unlink();(p/'test-root/package-ready.txt.part').mkdir();run(False,'full');assert not (p/'test-root/package-ready.txt').exists()
print('PASS first/full, cached, stale marker, size change, duplicate SD asset, forced corruption/retry, marker write failure')
