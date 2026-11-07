"""Explicit authenticated node-profile Apply client. No automatic mutation retry.

State/result queries are read-only. Apply requires a supplied global request ID,
revision, boot/precondition and idempotency token. No discovery, flashing, USB,
credential provisioning or broker operation is performed by this tool.
"""
import argparse
import json
import os
from pathlib import Path
import re
import ssl
from urllib.error import HTTPError
from urllib.parse import urlsplit
from urllib.request import Request,HTTPSHandler,build_opener
import zlib

from tools.target.network_client import NoRedirects


def _unique(pairs):
    result={}
    for name,value in pairs:
        if name in result:raise ValueError('duplicate reply field')
        result[name]=value
    return result


def _uint(value,minimum=0):
    return type(value) is int and minimum<=value<=0xffffffff


def _token(value):
    return isinstance(value,str) and re.fullmatch(r'[A-Za-z0-9._-]{1,64}',value) is not None


def _result(data,operation,status):
    if not isinstance(data,dict) or type(data.get('version')) is not int or data['version']!=1:
        raise ValueError('unsupported reply')
    if 'error' in data:
        if status<400 or data['error'] not in ('configuration_unavailable','request_unavailable'):
            raise ValueError('unknown error reply')
        return dict(version=1,error=data['error'])
    if not isinstance(data.get('boot_id'),str) or not re.fullmatch(r'[0-9a-f]{16}',data['boot_id']):
        raise ValueError('invalid boot identity')
    if operation=='state':
        integers=('last_request_id','active_revision','saved_revision','max_body')
        booleans=('durable_known','recovery','busy')
        if data.get('max_body')!=8192:
            raise ValueError('unsupported body bound')
    else:
        integers=('request_id','controller_status','active_revision','saved_revision','active_crc','saved_crc','flags')
        booleans=('historical',)
        if not _uint(data.get('controller_status')) or data['controller_status']>8 or not _uint(data.get('flags')) or data['flags']>127:
            raise ValueError('invalid controller outcome')
    if any(not _uint(data.get(field)) for field in integers) or any(type(data.get(field)) is not bool for field in booleans):
        raise ValueError('invalid controller fields')
    if operation!='state' and data['controller_status']==1:
        if (data['active_revision']==0 or data['active_revision']!=data['saved_revision'] or
                data['flags']&88!=88 or data['flags']&32):
            raise ValueError('inconsistent saved flags')
    # Whitelist fields so arbitrary remote error text/profile bytes are not logged.
    return {name:data[name] for name in ('version','boot_id',*integers,*booleans)}


def request(origin,operation,credential,ca,*,request_id=None,revision=None,
            expected=None,boot=None,request_token=None,profile=None):
    parts=urlsplit(origin)
    if (parts.scheme!='https' or not parts.hostname or parts.username or parts.password or
            parts.query or parts.fragment or parts.path not in ('','/')):
        raise ValueError('use an HTTPS origin')
    if (not isinstance(credential,str) or not 16<=len(credential)<=64 or
            any(not 33<=ord(char)<=126 for char in credential)):
        raise ValueError('invalid credential')
    if operation not in ('state','result','apply'):
        raise ValueError('invalid operation')
    headers={'Authorization':'Bearer '+credential,'Connection':'close'}
    path='/v1/configuration'
    body=None
    if operation!='state':
        if not _uint(request_id,1) or not _token(request_token) or not isinstance(boot,str) or not re.fullmatch(r'[0-9a-f]{16}',boot):
            raise ValueError('invalid request identity')
        headers.update({'X-Request-ID':str(request_id),'X-Request-Token':request_token})
        if operation=='result':
            path+='/result'
        else:
            if not _uint(revision,1) or not _uint(expected) or not isinstance(profile,bytes) or not 1<=len(profile)<=8192:
                raise ValueError('invalid profile bounds/preconditions')
            body=profile
            headers.update({'Content-Type':'application/json','Content-Length':str(len(body)),
                            'X-Revision':str(revision),'X-Expected-Revision':str(expected),
                            'X-Expected-Boot-ID':boot})
    context=ssl.create_default_context(cafile=ca)
    opener=build_opener(HTTPSHandler(context=context),NoRedirects())
    query=Request(origin.rstrip('/')+path,data=body,headers=headers,
                  method='POST' if operation=='apply' else 'GET')
    try:
        response=opener.open(query,timeout=30)
    except HTTPError as error:
        response=error
    with response:
        status=response.getcode()
        if response.headers.get_content_type()!='application/json':
            raise ValueError('unexpected response type')
        raw=response.read(513)
        if len(raw)>512:
            raise ValueError('response exceeds bound')
        result=_result(json.loads(raw,object_pairs_hook=_unique),operation,status)
    if operation!='state' and 'error' not in result:
        if result['boot_id']!=boot or result['request_id']!=request_id:
            raise ValueError('result belongs to another boot/request')
        if operation=='apply' and result['controller_status']==1:
            crc=zlib.crc32(profile)&0xffffffff
            if (result['active_revision']!=revision or result['saved_revision']!=revision or
                    result['active_crc']!=crc or result['saved_crc']!=crc or
                    result['flags']&24!=24 or result['flags']&32):
                raise ValueError('inconsistent saved result')
    saved=(result.get('controller_status')==1 and result.get('flags',0)&24==24)
    if operation=='state':
        outcome='state_observed'
    elif 'controller_status' not in result:
        outcome='not_confirmed'
    else:
        outcome={1:'saved_at_completion',2:'active_unsaved',3:'rejected',
                 4:'recovery_required',5:'stale',6:'conflict',7:'storage_error',
                 8:'empty'}.get(result['controller_status'],'not_confirmed')
    return dict(http_status=status,reply=result,saved_at_completion=saved,
                outcome=outcome,retry_performed=False)


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('origin');parser.add_argument('--ca',required=True)
    parser.add_argument('--token-env',default='UTHOME_TOKEN')
    parser.add_argument('--report',type=Path,help='reserve a new JSON file before contacting the ESP')
    sub=parser.add_subparsers(dest='operation',required=True)
    sub.add_parser('state')
    result=sub.add_parser('result');apply=sub.add_parser('apply')
    for command in (result,apply):
        command.add_argument('--request-id',type=int,required=True)
        command.add_argument('--boot-id',required=True)
        command.add_argument('--request-token',required=True,help='nonsecret operation identifier; retain it for result lookup')
    apply.add_argument('--revision',type=int,required=True)
    apply.add_argument('--expected-revision',type=int,required=True)
    apply.add_argument('--profile',type=Path,required=True)
    args=parser.parse_args(argv)
    output=None
    if args.report:
        try:output=args.report.open('x',encoding='utf-8')
        except OSError:parser.error('report must be a new writable file in an existing directory')
    record=dict(kind='configuration-client-result',operation=args.operation,
                whole_system_qualified=False)
    try:
        parameters={}
        if args.operation!='state':
            parameters=dict(request_id=args.request_id,boot=args.boot_id,request_token=args.request_token)
        if args.operation=='apply':
            # Bounded local read; preserve original bytes for exact retries/CRC.
            with args.profile.open('rb') as source:profile=source.read(8193)
            parameters.update(revision=args.revision,expected=args.expected_revision,profile=profile)
        result=request(args.origin,args.operation,os.environ.get(args.token_env,''),args.ca,**parameters)
        record.update(result)
        code=0 if (result['http_status']==200 and
                    (args.operation=='state' or result['saved_at_completion'])) else 1
    except KeyboardInterrupt:
        record.update(outcome='not_confirmed',error_type='KeyboardInterrupt',retry_performed=False);code=130
    except Exception as error:
        record.update(outcome='not_confirmed',error_type=type(error).__name__,retry_performed=False);code=1
    encoded=json.dumps(record,indent=2,allow_nan=False)+'\n'
    if output:
        with output:output.write(encoded)
    print(encoded,end='')
    return code


if __name__=='__main__':raise SystemExit(main())
