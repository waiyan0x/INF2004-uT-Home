"""Compare a frozen model with the independently measured timing rule."""
import argparse
import hashlib
from pathlib import Path

from ai.protocol_gate import assess
from ai.protocol_study import MAX_BYTES, dataset, observe_peers, validate_study
from ai.recording import load_recording, save_new
from ai.records import assess_run
from ai.timing import digest
from tools.config_validator.planner import load_document, require


def load_calibration(study, root, expected_sha256):
    """Replay separate calibration journals; never infer costs from trial labels."""
    validate_study(study)
    root = Path(root)
    path = root / 'calibrated-baseline.json'
    require(hashlib.sha256(path.read_bytes()).hexdigest() == expected_sha256,
            'Frozen calibrated baseline hash changed')
    baseline = load_document(path)
    summary_path = root / 'calibration/summary.json'
    require(baseline['study_sha256'] == digest(study)
            and baseline['calibration_summary_sha256'] == hashlib.sha256(summary_path.read_bytes()).hexdigest()
            and baseline['calibration_is_training_data'] is False, 'Calibration binding mismatch')
    summary = load_document(summary_path, MAX_BYTES)
    require(summary['error'] is None and summary['restored'] is True
            and summary['study_sha256'] == digest(study) and summary['families'] == baseline['families'],
            'Calibration did not complete cleanly')
    cases = {c['experiment']['scenario']['family_id']: c['experiment'] for c in study['cases']}
    entries = {e['family_id']: e for e in baseline['families']}
    require(len(entries) == len(baseline['families']) and set(entries) == set(cases),
            'Missing or duplicate independent calibration')
    identities = set()
    for family, entry in entries.items():
        folder = root / 'calibration' / family / 'observation'
        experiment, run = load_recording(folder)
        require(digest(experiment) == digest(cases[family]) and run['interval_ms'] == 500
                and run['repetition'] == 0 and run['sample_interval_ms'] == study['specification']['cadence_ms'],
                'Calibration differs from frozen workload or cadence')
        assessment = assess_run(experiment, run)
        require(assessment['usable'] and run['capture']['runtime_healthy']
                and assessment['unhealthy_samples'] == assessment['health_faults'] == 0
                and observe_peers(folder, experiment, run)['passes'], 'Unhealthy calibration')
        costs = load_document(folder / 'workcost.json')
        require(entry['healthy'] is True and entry['calibration_run_sha256'] == digest(run)
                and entry['raw_sha256'] == run['capture']['raw_sha256']
                and entry['workcost_sha256'] == hashlib.sha256((folder / 'workcost.json').read_bytes()).hexdigest(),
                'Calibration journal binding changed')
        workers = costs['window']['workers']
        require(all(w['timeouts'] == w['errors'] == 0 for w in workers), 'Unhealthy calibration transfers')
        source = next(i for i, d in enumerate(experiment['profile']['devices'])
                      if d['device_id'] == experiment['target']['instance_id'])
        target = next(w for w in workers if w['source_index'] == source)
        margin = (target['max_elapsed_us_since_reset'] + target['max_lateness_us_since_reset'] + 999) // 1000
        chosen = max((t['interval_ms'] for t in experiment['trials']
                      if t['interval_ms'] + margin <= experiment['scenario']['goal']['limit_ms']), default=None)
        require(entry['margin_ms'] == margin and entry['calibrated_rule_interval_ms'] == chosen,
                'Calibrated rule differs from independent measurements')
        identities.add(run['identity_evidence_sha256'])
    require(len(identities) == 1, 'Calibration needs one board/build identity')
    return dict(baseline, identity_sha256=identities.pop(), baseline_file_sha256=expected_sha256)


def assess_calibrated(study, model, data, baseline):
    """Add the predeclared comparison without weakening the existing gate."""
    report = assess(study, model, data)
    require(baseline['study_sha256'] == digest(study), 'Calibration study changed')
    entries = {e['family_id']: e for e in baseline['families']}
    families = {f for ids in study['split'].values() for f in ids}
    require(len(entries) == len(baseline['families']) and set(entries) == families,
            'Missing or duplicate calibration family')
    calibration_runs = {e['calibration_run_sha256'] for e in entries.values()}
    calibration_raw = {e['raw_sha256'] for e in entries.values()}
    require(all(e['run_sha256'] not in calibration_runs and e['raw_sha256'] not in calibration_raw
                for row in data['rows'] for e in row['evidence']), 'Calibration leaked into labelled trials')
    partitions = {}
    for partition, selected in study['split'].items():
        decisions = {}
        for family in selected:
            rows = [r for r in data['rows'] if r['family_id'] == family]
            passing = {r['interval_ms'] for r in rows if r['label'] == 1}
            chosen = entries[family]['calibrated_rule_interval_ms']
            require(chosen is None or chosen in {r['interval_ms'] for r in rows}, 'Unplanned calibrated interval')
            tree = report['partitions'][partition]['families'][family]['tree_ms']
            safe_tree, safe_rule = tree in passing, chosen in passing
            decisions[family] = dict(tree_ms=tree, calibrated_rule_ms=chosen,
                margin_ms=entries[family]['margin_ms'], calibrated_rule_violation=chosen is not None and not safe_rule,
                calibrated_rule_abstention=chosen is None,
                safe_coverage_gain=safe_tree and not safe_rule,
                safe_interval_gain=safe_tree and safe_rule and tree > chosen,
                regression_against_safe_calibrated_rule=safe_rule and (not safe_tree or tree < chosen))
        partitions[partition] = decisions
    heldout = [v for part in ('validation', 'test') for v in partitions[part].values()]
    reasons = list(report['reasons'])
    if any(v['regression_against_safe_calibrated_rule'] for v in heldout):
        reasons.append('heldout_regression_against_safe_calibrated_rule')
    if not any(v['safe_coverage_gain'] or v['safe_interval_gain'] for v in heldout):
        reasons.append('no_measured_benefit_over_calibrated_rule')
    return dict(contract='uthome.calibrated-model-gate', version='0.1.0', study_sha256=digest(study),
                model_sha256=digest(model), baseline_file_sha256=baseline['baseline_file_sha256'],
                existing_gate=report, partitions=partitions, statistical_criteria_passed=not reasons,
                reasons=reasons, deployment_allowed=False, automatic_apply=False,
                limitation='Empirical service-time margin, not WCET, CPU use or a scheduling guarantee')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('study', 'model', 'calibration-root', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--baseline-sha256', required=True, help='Hash frozen before labelled trials')
    parser.add_argument('recordings', nargs='+', type=Path)
    args = parser.parse_args(argv)
    study = load_document(args.study, MAX_BYTES)
    baseline = load_calibration(study, args.calibration_root, args.baseline_sha256)
    data = dataset(study, args.recordings)
    for folder in args.recordings:
        _, run = load_recording(folder)
        require(run['identity_evidence_sha256'] == baseline['identity_sha256'], 'Calibration/trial identity changed')
    result = assess_calibrated(study, load_document(args.model), data, baseline)
    save_new(args.output, result)
    print('Statistical criteria:', 'passed' if result['statistical_criteria_passed'] else 'failed')
    print('Deployment remains disabled.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
