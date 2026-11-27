"""Synthetic recovery, chunk-boundary invariance, and optional saved-IQ regression."""
import argparse,json,subprocess,time
from pathlib import Path
import numpy as np
from scipy import signal

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'local/tests';OUT.mkdir(parents=True,exist_ok=True)

def run(iq,name,chunk=4096,rate=9600,rrc_enabled=True):
    dest=OUT/f'{name}.u8'
    start=time.monotonic()
    p=subprocess.run([str(ROOT/'build/dqpsk_core_cli.exe'),'-',str(dest),str(rate),'300',str(chunk),str(int(rrc_enabled))],input=np.asarray(iq,dtype=np.complex64).tobytes(),capture_output=True,check=True)
    rows=[json.loads(s) for s in p.stdout.decode().splitlines()]
    (OUT/f'{name}.json').write_text(json.dumps(rows,indent=2))
    return np.fromfile(dest,dtype=np.uint8),rows,time.monotonic()-start

def rrc(fs,rate,alpha=1):
    t=np.arange(-int(6*fs/rate),int(6*fs/rate)+1)*rate/fs
    h=np.empty_like(t)
    for i,a in enumerate(t):
        if abs(a)<1e-9:h[i]=1+alpha*(4/np.pi-1)
        elif abs(abs(a)-1/(4*alpha))<1e-8:h[i]=alpha/np.sqrt(2)*((1+2/np.pi)*np.sin(np.pi/(4*alpha))+(1-2/np.pi)*np.cos(np.pi/(4*alpha)))
        else:h[i]=(np.sin(np.pi*a*(1-alpha))+4*alpha*a*np.cos(np.pi*a*(1+alpha)))/(np.pi*a*(1-16*alpha*alpha*a*a))
    return h/np.sqrt(np.sum(h*h))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--recorded-npz',type=Path);parser.add_argument('--template',type=Path)
    args=parser.parse_args();rng=np.random.default_rng(858)
    labels=rng.integers(0,4,48000,dtype=np.uint8)
    points=np.exp(1j*np.cumsum(np.pi/4+labels.astype(float)*np.pi/2))
    tx=np.zeros(len(points)*5,dtype=np.complex128);tx[::5]=points
    tx=signal.fftconvolve(tx,rrc(48000,9600),mode='full')
    iq=signal.resample_poly(tx,25,24)
    iq*=np.exp(2j*np.pi*337*np.arange(len(iq))/50000)
    iq+=.005*(rng.standard_normal(len(iq))+1j*rng.standard_normal(len(iq)))
    a,rows,wall=run(iq,'synthetic-4096')
    # Random information, so a short leading decision sequence uniquely fixes alignment.
    scores=[np.mean(a[:5000]==labels[k:k+5000]) for k in range(64)]
    lag=int(np.argmax(scores));agreement=float(np.mean(a==labels[lag:lag+len(a)]))
    assert agreement>.9999,(lag,agreement)
    assert all(r['locked'] for r in rows)
    b,_,_=run(iq,'synthetic-137',137);assert np.array_equal(a,b),'Feed chunk size changed decisions'
    noise=.01*(rng.standard_normal(160000)+1j*rng.standard_normal(160000))
    _,noiseRows,_=run(noise,'noise');assert not any(r['locked'] for r in noiseRows)
    _,cwRows,_=run(np.exp(2j*np.pi*337*np.arange(160000)/50000),'cw');assert not any(r['locked'] for r in cwRows)
    result={'synthetic':{'symbols':len(a),'agreement':agreement,'alignment':lag,'lockedBlocks':len(rows),'chunkBoundaryInvariant':True,'wallSeconds':wall},'noiseLocks':sum(r['locked'] for r in noiseRows)}
    # H-DQPSK symbol reference: 6000/s, pi/4 phase increments, raised-cosine TX pulse.
    hlabels=rng.integers(0,4,36000,dtype=np.uint8)
    hpoints=np.exp(1j*np.cumsum(np.pi/4+hlabels.astype(float)*np.pi/2))
    ht=np.arange(-48,49)/8;hpulse=np.sinc(ht)*np.cos(np.pi*ht)
    good=np.abs(1-4*ht*ht)>1e-8
    hpulse[good]/=(1-4*ht[good]*ht[good]);hpulse[~good]=.5
    htx=np.zeros(len(hpoints)*8,dtype=np.complex128);htx[::8]=hpoints
    hiq=signal.resample_poly(signal.fftconvolve(htx,hpulse,mode='full'),25,24)
    hiq*=np.exp(2j*np.pi*337*np.arange(len(hiq))/50000)
    hiq+=.003*(rng.standard_normal(len(hiq))+1j*rng.standard_normal(len(hiq)))
    ha,hrows,hwall=run(hiq,'hdqpsk-6000',rate=6000,rrc_enabled=False)
    hlags=[np.mean(ha[:5000]==hlabels[k:k+5000]) for k in range(64)];hlag=int(np.argmax(hlags))
    hagree=float(np.mean(ha==hlabels[hlag:hlag+len(ha)]));assert hagree>.9999 and all(r['locked'] for r in hrows)
    result['HDQPSK6000RaisedCosineReference']={'symbols':len(ha),'agreement':hagree,'alignment':hlag,'lockedBlocks':len(hrows),'wallSeconds':hwall,'scope':'Raw phase/dibit recovery; no TDMA, protocol or transmitter conformance validation'}
    if args.recorded_npz:
        iq=np.load(args.recorded_npz)['iq']
        a,rows,wall=run(iq,'recorded');template=np.load(args.template)
        score=[np.mean(a[:9600]==np.resize(np.roll(template,-k),9600)) for k in range(len(template))]
        k=int(np.argmax(score));errors=int(np.sum(a!=np.resize(np.roll(template,-k),len(a))))
        result['recorded']={'decisions':len(a),'templateOffset':k,'templateDisagreements':errors,'lockedBlocks':sum(r['locked'] for r in rows),'blocks':len(rows),'wallSeconds':wall,'dataSeconds':len(iq)/50000}
        assert errors==0,result['recorded']
        assert all(r['locked'] for r in rows)
    (OUT/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
