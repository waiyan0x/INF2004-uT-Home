"""Relink the pinned Windows ESP build with TLS counters. No board access."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def quoted(path):
    return '"' + path.as_posix() + '"'


def run_compiler(compiler, options, response, build):
    response.write_text(options, encoding='utf-8')
    subprocess.run([compiler, '@' + str(response)], cwd=build, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path, help='qualified ESP Ninja build directory')
    parser.add_argument('output', type=Path, help='new directory for diagnostic artifacts')
    args = parser.parse_args()
    build = args.build.resolve()
    output = args.output.resolve()
    project = json.loads((build / 'project_description.json').read_text())
    if (project['target'], project['app_bin']) != ('esp32c3', 'uthome_runtime_port.bin'):
        parser.error('expected the qualified ESP32-C3 runtime target')
    output.mkdir(parents=True, exist_ok=False)
    commands = json.loads((build / 'compile_commands.json').read_text())
    peer, = [row for row in commands if Path(row['file']).name == 'c3peer.c']
    compiler, flags = peer['command'].split(' ', 1)
    obj = output / 'tlsheap.obj'
    source = Path(__file__).resolve().with_name('tlsheap.c')
    options = (flags.split(' -o ')[0] + ' -I' + quoted(Path(peer['file']).parent)
               + ' -o ' + quoted(obj) + ' -c ' + quoted(source))
    run_compiler(compiler, options, output / 'compile.rsp', build)

    # Reuse the pinned Ninja link inputs, including the SDK scripts and library order.
    elf = project['app_elf']
    rule = (build / 'build.ninja').read_text().split('build ' + elf + ': ', 1)[1].split('\n\n', 1)[0]
    fields = dict(re.findall(r'^  (\w+) = (.*)$', rule, re.M))
    link_input = rule.split(' |', 1)[0].split(' ', 1)[1]
    linker = Path(compiler).with_name(Path(compiler).name.replace('gcc', 'g++'))
    bindings = {'esp_tls_conn_new_async': 'connect', 'esp_mbedtls_mem_calloc': 'alloc',
                'esp_mbedtls_mem_free': 'free', 'mbedtls_ssl_handshake_client_step': 'step'}
    for name in ('control', Path(elf).stem):
        target = output / name
        baseline_map = (build / Path(elf).with_suffix('.map')).as_posix()
        if fields['LINK_FLAGS'].count(baseline_map) != 1:
            raise ValueError('unexpected map output in qualified link flags')
        flags = fields['LINK_FLAGS'].replace(baseline_map, quoted(target.with_suffix('.map')))
        parts = [fields['FLAGS'], flags, link_input]
        if name != 'control':
            parts.append(quoted(obj))
            for sdk, hook in bindings.items():
                parts.append(f'-Wl,--wrap={sdk},--defsym=__wrap_{sdk}=tlsheap_{hook},'
                             f'--defsym=tlsheap_native_{hook}=__real_{sdk}')
        parts.extend([fields['LINK_PATH'], fields['LINK_LIBRARIES'], '-o', quoted(target.with_suffix('.elf'))])
        run_compiler(linker, ' '.join(parts), target.with_suffix('.rsp'), build)
        image = target.with_suffix('.bin')
        subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32c3', 'elf2image',
                        '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '4MB',
                        '--elf-sha256-offset', '0xb0', '--flash-mmu-page-size', '64KB',
                        '--min-rev', '3', '--min-rev-full', '3', '--max-rev-full', '199',
                        '-o', image, target.with_suffix('.elf')], check=True)
        checksum = digest(image)
        if name == 'control' and checksum != digest(build / project['app_bin']):
            raise ValueError('control image differs from the qualified firmware')
        print(f'{image.name}  {checksum}')


if __name__ == '__main__':
    main()
