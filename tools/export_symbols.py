"""Export native symbol JSONL without inserting padding or bridging missing data."""
import argparse,base64,hashlib,json
from pathlib import Path

def unpack(encoded,count):
    data=base64.b64decode(encoded,validate=True)
    if len(data)!=(count+3)//4:raise ValueError('Invalid packed-symbol length')
    return bytes((data[i//4]>>(6-2*(i%4)))&3 for i in range(count))

def export(source,directory):
    directory.mkdir(parents=True,exist_ok=True)
    manifest={'format':'sdrangel-dqpsk-export-v1','source':source.name,'interpretation':'Phase labels and provisional channel bits, not application data','parts':[]}
    header=None;part=None;symbol_file=bit_file=None;acc=used=0;previous=None
    def close_part():
        nonlocal part,symbol_file,bit_file,acc,used
        if part is None:return
        if used:bit_file.write(bytes([acc<<(8-used)]))
        symbol_file.close();bit_file.close()
        part['validBits']=part['symbols']*2;part['paddingBits']=(-part['validBits'])%8
        for key in ['symbolsFile','bitsFile']:
            path=directory/part[key]
            with path.open('rb') as data:part[key+'SHA256']=hashlib.file_digest(data,'sha256').hexdigest()
        manifest['parts'].append(part);part=None;acc=used=0
    try:
        for line_number,line in enumerate(source.open(encoding='utf-8'),1):
            record=json.loads(line)
            kind=record.get('type')
            if kind=='header':
                if header is not None or record.get('format')!='sdrangel-dqpsk-symbols-v1':raise ValueError('Unsupported or duplicate header')
                header=record;manifest['settings']=record['settings'];continue
            if header is None:raise ValueError('Missing symbol-file header')
            if kind!='symbols':
                if kind in ['gap','segment','end']:close_part();previous=None
                continue
            count=record['count']
            if not isinstance(count,int) or not 0<count<=12001:raise ValueError('Invalid block symbol count')
            symbols=unpack(record['phaseLabels2bitBase64'],count)
            mapping=header['settings']['dibitMap']
            if sorted(mapping)!=[0,1,2,3]:raise ValueError('Invalid provisional mapping')
            decoded=unpack(record['provisionalBitsBase64'],count)
            if decoded!=bytes(mapping[v] for v in symbols):raise ValueError('Stored provisional bits disagree with phase labels')
            if len(base64.b64decode(record['phaseErrorsInt8Base64'],validate=True))!=count:raise ValueError('Invalid phase-quality length')
            if previous:
                expected=previous['firstSample']+previous['count']*previous['samplesPerSymbol']
                contiguous=(record['segment']==previous['segment'] and record['sequence']==previous['sequence']+1 and abs(record['firstSample']-expected)<.6*record['samplesPerSymbol'])
                if not contiguous:close_part()
            if part is None:
                name=f'part-{len(manifest["parts"]):03d}'
                part={'segment':record['segment'],'firstSample':record['firstSample'],'symbols':0,'symbolsFile':name+'.symbols.u8','bitsFile':name+'.bits.bin','firstSequence':record['sequence']}
                # A new output directory is expected; never overwrite prior exported data.
                symbol_file=(directory/part['symbolsFile']).open('xb');bit_file=(directory/part['bitsFile']).open('xb')
            symbol_file.write(symbols);packed=bytearray()
            for label in decoded:
                acc=(acc<<2)|label;used+=2
                if used==8:packed.append(acc);acc=used=0
            bit_file.write(packed);part['symbols']+=count;part['lastSequence']=record['sequence'];previous=record
        close_part()
    finally:
        if symbol_file and not symbol_file.closed:symbol_file.close()
        if bit_file and not bit_file.closed:bit_file.close()
    if header is None:raise ValueError('Empty symbol capture')
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output_directory',type=Path);a=p.parse_args()
    result=export(a.source,a.output_directory)
    print(json.dumps({'parts':len(result['parts']),'symbols':sum(v['symbols'] for v in result['parts']),'output':str(a.output_directory)},indent=2))
