"""Verify retained capture batches and list missing runs without board access."""
import argparse
import hashlib
from pathlib import Path

from ai.protocol_study import MAX_BYTES, validate_study
from ai.recording import load_recording, save_new
from ai.records import assess_run
from ai.timing import digest
from tools.config_validator.planner import load_document, require


def resume_plan(study, identity, captures, families, *, collect_costs=False, bindings=None):
    """Keep every complete outcome; refuse interrupted batches or omitted history."""
    validate_study(study)
    known = {c['experiment']['scenario']['family_id']: c['experiment'] for c in study['cases']}
    require(families and len(families) == len(set(families)) and set(families) <= set(known),
            'Invalid selected families')
    roots = [Path(p).resolve() for p in captures]
    require(len(roots) == len(set(roots)), 'Duplicate capture folder')
    hashes = {str(p): hashlib.sha256((p / 'summary.json').read_bytes()).hexdigest() for p in roots}
    completed = {}
    for root in roots:
        summary = load_document(root / 'summary.json', MAX_BYTES)
        require(summary['study_sha256'] == digest(study)
                and digest(load_document(root / 'study.json', MAX_BYTES)) == digest(study)
                and digest(load_document(root / 'identity.json')) == digest(identity),
                'Previous capture study or identity changed')
        require(summary['restored'] is True
                and summary['error'] in (None, 'Collection stopped by operator'),
                'Previous capture failed or restoration is unverified; review before resuming')
        require(summary['measured_costs'] is collect_costs, 'Observer cost-measurement mode changed')
        for previous in summary.get('previous_captures', []):
            require(hashes.get(previous['folder']) == previous['summary_sha256'],
                    'Missing or changed earlier capture history')
        prior_bindings = load_document(root / 'static-bindings.json', MAX_BYTES) if bindings is not None else {}
        listed = set()
        for item in summary['runs']:
            family, interval, repetition = (item[k] for k in ('family_id', 'interval_ms', 'repetition'))
            require(family in known and family in summary['selected_families'], 'Unplanned previous family')
            folder = root / family / f'run-{interval}-{repetition}'
            listed.add(folder)
            experiment, run = load_recording(folder)
            require(digest(experiment) == digest(known[family])
                    and (run['interval_ms'], run['repetition']) == (interval, repetition)
                    and run['identity_evidence_sha256'] == digest(identity)
                    and run['sample_interval_ms'] == study['specification']['cadence_ms'],
                    'Previous run differs from frozen study, identity or cadence')
            require(item['usable'] is True and assess_run(experiment, run)['usable'],
                    'Interrupted previous run; no automatic retry')
            if collect_costs:
                require((folder / 'workcost.json').is_file(), 'Missing previous measured costs')
            if bindings is not None and family in bindings:
                require(prior_bindings.get(family) == bindings[family], 'Execution context changed')
            key = (family, interval, repetition)
            require(key not in completed, 'Duplicate previous trial/repetition')
            completed[key] = dict(family_id=family, interval_ms=interval, repetition=repetition,
                                  folder=str(folder), run_sha256=digest(run),
                                  raw_sha256=run['capture']['raw_sha256'])
        require(listed == set(root.glob('*/run-*')), 'Unlisted attempted run; review before resuming')
    missing = [dict(family_id=family, interval_ms=t['interval_ms'], repetition=rep)
               for family in families for t in known[family]['trials']
               for rep in range(known[family]['scenario']['repetitions'])
               if (family, t['interval_ms'], rep) not in completed]
    return dict(study_sha256=digest(study), identity_sha256=digest(identity),
                previous_captures=[dict(folder=name, summary_sha256=value) for name, value in hashes.items()],
                completed_runs=list(completed.values()), missing_runs=missing,
                board_accessed=False, deployment_allowed=False)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('study', 'identity', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--captures', required=True, nargs='+', type=Path)
    parser.add_argument('--families', required=True, nargs='+')
    parser.add_argument('--measure-costs', action='store_true')
    args = parser.parse_args(argv)
    study = load_document(args.study, MAX_BYTES)
    families = list(study['split']['train'] + study['split']['validation'] + study['split']['test']) \
        if args.families == ['all'] else args.families
    plan = resume_plan(study, load_document(args.identity), args.captures, families,
                       collect_costs=args.measure_costs)
    save_new(args.output, plan)
    print(f"Verified {len(plan['completed_runs'])} retained runs; {len(plan['missing_runs'])} selected runs remain. No board access.")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
