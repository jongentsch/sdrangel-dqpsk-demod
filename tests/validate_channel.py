"""Exercise the actual native plugin, channelizer, control API and capture limits."""
import argparse,base64,json,os,subprocess,time
from pathlib import Path
import requests
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'local/native-tests';OUT.mkdir(parents=True,exist_ok=True)
URL='http://127.0.0.1:8093'

def get(path=''):
    r=requests.get(URL+path,timeout=2);r.raise_for_status();return r.json()
def post(path,body):
    r=requests.post(URL+path,json=body,timeout=2);r.raise_for_status();return r.json()
def wait_idle(uid,timeout=8):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        r=get('/channels/'+uid)['report']
        if not r['recording']:return r
        time.sleep(.1)
    raise AssertionError('Capture did not stop automatically')
def rows(path):return [json.loads(s) for s in Path(path).read_text().splitlines()]
def labels(records):
    out=[]
    for r in records:
        if r['type']!='symbols':continue
        raw=np.frombuffer(base64.b64decode(r['phaseLabels2bitBase64']),dtype=np.uint8)
        out.append(np.column_stack([(raw>>s)&3 for s in [6,4,2,0]]).ravel()[:r['count']])
    return np.concatenate(out) if out else np.array([],dtype=np.uint8)
def main():
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('template',type=Path);args=p.parse_args()
    env=os.environ.copy();env['PATH']=r'C:\Program Files\SDRangel;'+env['PATH']
    log=(OUT/'native-process.log').open('wb')
    proc=subprocess.Popen([str(ROOT/'build/dqpsk_channel_test.exe'),str(ROOT/'build/demoddqpsk.dll'),str(args.capture),'16'],env=env,stdout=log,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        end=time.monotonic()+5;state=None
        while time.monotonic()<end:
            try:
                state=get()
                if state['channels'] and state['channels'][0]['report']['running']:break
            except requests.RequestException:pass
            time.sleep(.1)
        assert state and len(state['channels'])==1
        uid=state['channels'][0]['report']['uid'];base='/channels/'+uid
        # Native settings validation must reject invalid mappings.
        invalid=requests.patch(URL+base+'/settings',json={'dibitMap':[0,0,2,3]},timeout=2)
        assert invalid.status_code==409
        post(base+'/capture/start',{'directory':str(OUT.resolve()),'seconds':5,'maxBytes':1024*1024})
        timed=wait_idle(uid);first=rows(timed['captureFile']);recovered=labels(first)
        assert first[-1]['type']=='end' and first[-1]['reason']=='time limit reached'
        assert len(recovered)>20000,(timed,first)
        template=np.load(args.template)
        offsets=[np.mean(recovered[:9600]==np.resize(np.roll(template,-k),9600)) for k in range(576)]
        offset=int(np.argmax(offsets));errors=int(np.sum(recovered!=np.resize(np.roll(template,-offset),len(recovered))))
        assert errors==0,errors
        post(base+'/capture/start',{'directory':str(OUT.resolve()),'seconds':8,'maxBytes':32768})
        limited=wait_idle(uid);second=rows(limited['captureFile'])
        assert second[-1]['reason']=='byte limit reached',second[-1]
        assert Path(limited['captureFile']).stat().st_size<=32768
        # It must stay stopped; a stale start flag must not reopen a fresh file.
        time.sleep(.3);assert not get(base)['report']['recording']
        post(base+'/capture/start',{'directory':str(OUT.resolve()),'seconds':5,'maxBytes':1024*1024})
        time.sleep(.2);post(base+'/capture/stop',{});manual=wait_idle(uid)
        assert rows(manual['captureFile'])[-1]['reason']=='stopped'
        result={'nativeChannelSymbolsCompared':len(recovered),'templateDisagreements':errors,'templateOffset':offset,'timedCaptureBytes':Path(timed['captureFile']).stat().st_size,
            'timedCaptureAutoStopped':True,'byteLimitedCaptureBytes':Path(limited['captureFile']).stat().st_size,'byteLimitAutoStopped':True,'manualStopPassed':True,
            'noAutomaticReopen':True,'invalidMappingRejected':True,'finalReport':get(base)['report']}
        (OUT/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='finalReport'},indent=2))
        proc.wait(timeout=20);assert proc.returncode==0,proc.returncode
    finally:
        if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
        log.close()
if __name__=='__main__':main()
