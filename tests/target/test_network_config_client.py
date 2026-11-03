"""Mutating client tests use a mocked HTTPS opener; no live POST is sent."""
import contextlib
from email.message import Message
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock,patch
from urllib.error import HTTPError
import zlib
from tools.target import network_config as client

SECRET='0123456789abcdef0123456789abcdef'
BODY=b'{"value":1}'
BOOT='0000000000000001'

def fields(status=1):
    crc=zlib.crc32(BODY)&0xffffffff
    return dict(version=1,boot_id=BOOT,request_id=1,controller_status=status,
                active_revision=1,saved_revision=1 if status==1 else 0,
                active_crc=crc,saved_crc=crc if status==1 else 0,
                flags=95 if status==1 else 71,historical=False)

class Response(io.BytesIO):
    def __init__(self,value,status=200):
        super().__init__(json.dumps(value).encode());self.status=status
        self.headers=Message();self.headers['Content-Type']='application/json'
    def getcode(self):return self.status

class ConfigClientTests(unittest.TestCase):
    def call(self,response=None,error=None,**changed):
        opener=Mock()
        if error:opener.open.side_effect=error
        else:opener.open.return_value=Response(fields() if response is None else response)
        options=dict(request_id=1,revision=1,expected=0,boot=BOOT,request_token='op',profile=BODY)
        options.update(changed)
        with patch.object(client.ssl,'create_default_context',return_value=None) as tls, \
             patch.object(client,'build_opener',return_value=opener):
            result=client.request('https://node:8443','apply',SECRET,'ca.pem',**options)
        tls.assert_called_once_with(cafile='ca.pem')
        opener.open.assert_called_once()
        return result,opener

    def test_post_preserves_exact_body_global_id_and_preconditions(self):
        result,opener=self.call()
        request=opener.open.call_args.args[0]
        headers={key.lower():value for key,value in request.header_items()}
        self.assertEqual(request.method,'POST')
        self.assertEqual(request.data,BODY)
        self.assertEqual(headers['x-request-id'],'1')
        self.assertEqual(headers['x-expected-boot-id'],BOOT)
        self.assertEqual(headers['x-expected-revision'],'0')
        self.assertEqual(headers['content-length'],'11')
        self.assertTrue(result['saved_at_completion'])
        self.assertFalse(result['retry_performed'])
        self.assertNotIn(SECRET,json.dumps(result))
        self.assertEqual(opener.open.call_args.kwargs['timeout'],30)

    def test_uncertain_save_http_error_is_returned_without_retry(self):
        response=Response(fields(2),503)
        error=HTTPError('https://node',503,'remote secret must not be printed',response.headers,response)
        result,opener=self.call(error=error)
        self.assertEqual(result['http_status'],503)
        self.assertEqual(result['reply']['controller_status'],2)
        self.assertFalse(result['saved_at_completion'])
        self.assertEqual(opener.open.call_count,1)

    def test_completion_outcomes_are_explicit_without_replay(self):
        for status,outcome,http in [(1,'saved_at_completion',200),(2,'active_unsaved',503),
                                    (3,'rejected',422),(4,'recovery_required',503),
                                    (5,'stale',409),(6,'conflict',409),(7,'storage_error',503)]:
            with self.subTest(status=status):
                response=Response(fields(status),http)
                if http==200:result,opener=self.call()
                else:
                    error=HTTPError('https://node',http,'failure',response.headers,response)
                    result,opener=self.call(error=error)
                self.assertEqual(result['outcome'],outcome)
                self.assertEqual(opener.open.call_count,1)
                self.assertFalse(result['retry_performed'])

    def test_bad_metadata_or_body_fails_before_network(self):
        original=dict(request_id=1,revision=1,expected=0,boot=BOOT,request_token='op',profile=BODY)
        for changed in [dict(request_id=0),dict(revision=0),dict(expected=-1),
                        dict(request_token='bad/token'),dict(boot='1'),
                        dict(profile=b'x'*8193),dict(profile=b'')]:
            with self.subTest(changed=changed),patch.object(client,'build_opener') as build:
                with self.assertRaises(ValueError):
                    client.request('https://node','apply',SECRET,'ca.pem',**{**original,**changed})
                build.assert_not_called()

    def test_mismatched_boot_crc_and_flags_are_not_confirmed_saved(self):
        for changed in [dict(boot_id='0000000000000002'),dict(active_crc=0),
                        dict(flags=7),dict(saved_revision=2),dict(request_id=2)]:
            with self.subTest(changed=changed),self.assertRaises(ValueError):
                self.call(response={**fields(),**changed})

    def test_read_only_state_and_historical_result_do_not_post(self):
        state=dict(version=1,boot_id=BOOT,last_request_id=2,active_revision=1,
                   saved_revision=1,durable_known=True,recovery=False,busy=False,max_body=8192)
        for operation,value in [('state',state),('result',dict(fields(),historical=True))]:
            opener=Mock();opener.open.return_value=Response(value)
            kwargs={} if operation=='state' else dict(request_id=1,boot=BOOT,request_token='op')
            with patch.object(client.ssl,'create_default_context',return_value=None),patch.object(client,'build_opener',return_value=opener):
                result=client.request('https://node',operation,SECRET,'ca.pem',**kwargs)
            self.assertEqual(opener.open.call_args.args[0].method,'GET')
            self.assertIsNone(opener.open.call_args.args[0].data)
            self.assertFalse(result['retry_performed'])

    def test_report_reserved_before_contact_and_errors_do_not_log_secrets(self):
        with tempfile.TemporaryDirectory() as folder:
            target=Path(folder)/'result.json';target.write_text('keep')
            with patch.object(client,'request') as request,contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                client.main(['https://node','--ca','ca.pem','--report',str(target),'state'])
            request.assert_not_called();self.assertEqual(target.read_text(),'keep')
            target=Path(folder)/'new.json'
            with patch.object(client,'request',side_effect=TimeoutError('secret')),contextlib.redirect_stdout(io.StringIO()):
                code=client.main(['https://node','--ca','ca.pem','--report',str(target),'state'])
            self.assertEqual(code,1)
            self.assertNotIn('secret',target.read_text())
            self.assertEqual(json.loads(target.read_text())['outcome'],'not_confirmed')

if __name__=='__main__':unittest.main()
