"""Read-only flash-size attribution. Never changes partitions or budget gates."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def factory_partition(data):
    for offset in range(0,len(data)-31,32):
        magic,kind,subtype,address,size,label,flags=struct.unpack_from('<2sBBII16sI',data,offset)
        if magic==b'\xff\xff':
            break
        if magic==b'\xeb\xeb':
            continue
        if magic!=b'\xaa\x50':
            raise ValueError('unsupported partition table')
        if kind==0 and subtype==0:
            return dict(offset=address,size=size,label=label.split(b'\0')[0].decode('ascii'))
    raise ValueError('factory application partition not found')


def rank_archives(files):
    totals={}
    for name,value in files.items():
        # Size JSON includes BSS and IRAM too. Rank mapped flash only, not total
        # object bytes, and never present these values as exact removable space.
        archive=name.rsplit(':',1)[0]
        flash=sum(group.get('size',0) for kind,group in value.get('memory_types',{}).items()
                  if kind in ('Flash Code','Flash Data'))
        totals[archive]=totals.get(archive,0)+flash
    return [dict(archive=name,flash_mapped_bytes=size)
            for name,size in sorted(totals.items(),key=lambda item:(-item[1],item[0])) if size]


def analyze(build,top=15):
    build=Path(build).resolve()
    evidence=build/'evidence.json'
    record=json.loads(evidence.read_text())
    binary=build/'build/uthome_runtime_port.bin'
    mapping=build/'build/uthome_runtime_port.map'
    partitions=build/'build/partition_table/partition-table.bin'
    for name in (binary,mapping,partitions):
        if not name.is_file():
            raise ValueError('complete linked artifacts required')
        relative=name.relative_to(build).as_posix()
        if record.get('build_status')=='passed' and record.get('artifacts',{}).get(relative,{}).get('sha256')!=digest(name):
            raise ValueError('qualified artifact hash mismatch')
    output=subprocess.run([sys.executable,'-m','esp_idf_size','--format','json2','--files',str(mapping)],
                          check=True,capture_output=True,text=True).stdout
    ranking=rank_archives(json.loads(output))
    partition=factory_partition(partitions.read_bytes())
    size=binary.stat().st_size
    return dict(kind='flash-size-analysis',build=str(build),build_status=record['build_status'],
                qualification_result=False,binary_bytes=size,partition=partition,
                partition_headroom_bytes=partition['size']-size,
                evidence_sha256=digest(evidence),map_sha256=digest(mapping),
                binary_sha256=digest(binary),partition_table_sha256=digest(partitions),
                largest_archives=ranking[:top],
                mapped_flash_total=sum(row['flash_mapped_bytes'] for row in ranking),
                note='Mapped flash ranking excludes IRAM load copies, image headers and padding. Archive costs are not exact removable bytes; shared dependencies and code versions matter. Failed builds remain failed.')


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--top',type=int,choices=range(1,31),default=15)
    args=parser.parse_args(argv)
    if args.output.exists():
        parser.error('use a new output path')
    result=analyze(args.build,args.top)
    with args.output.open('x',encoding='utf-8') as output:
        json.dump(result,output,indent=2);output.write('\n')
    print(json.dumps({name:result[name] for name in ('build_status','binary_bytes','partition_headroom_bytes','largest_archives')},indent=2))
    return 0


if __name__=='__main__':
    raise SystemExit(main())
