"""Resolve observed identity through reviewed catalogue data; no hardware I/O."""
from copy import deepcopy
from dataclasses import asdict

from tools.config_validator import Catalogue, load_document
from tools.config_validator.planner import ROOT, check, require


def identify_uart(observation, capabilities, preset, *, catalogue=None, policy=None):
    """A compatible report stream is a candidate, never a verified model."""
    catalogue = catalogue or Catalogue()
    policy = load_document(ROOT / 'catalogue/discovery.json') if policy is None else policy
    check(policy, load_document(ROOT / 'schemas/discovery-catalogue.schema.json'), 'discovery')
    require(observation.preset_code == preset['wire_code'] == 2 and
            capabilities.board == preset['board']['variant'] and capabilities.features & 4,
            'Unsupported UART discovery connection')
    record = observation.records[0] if len(observation.records) == 1 else {}
    matches = []
    for entry in policy['entries']:
        rule, ref = entry['identification'], entry['descriptor']
        require((ref['id'], ref['version']) in catalogue.descriptors, 'Invalid discovery descriptor reference')
        if rule['method'] != 'uart_reports':
            continue
        descriptor = catalogue.descriptors[(ref['id'], ref['version'])]
        require(descriptor['protocol'] == 'uart', 'Identification protocol mismatch')
        if (observation.result == 'complete' and record.get('parser_id') == rule['parser_id'] and
                record.get('window_ms') == rule['window_ms'] and
                record.get('valid_reports', 0) >= rule['minimum_valid_reports'] and
                all(record.get(key) == 0 for key in ('receive_errors', 'framing_errors', 'discarded_bytes'))):
            matches.append(ref)
    require(len(matches) <= 8, 'Discovery candidate limit exceeded; review catalogue')
    status = 'candidate' if len(matches) == 1 else 'ambiguous' if matches else 'unknown'
    device = identity_result(record, matches, status, capabilities)
    return dict(contract_version='0.1.0', catalogue_version=policy['catalogue_version'],
                board=deepcopy(preset['board']), preset_id=preset['preset_id'],
                preset_version=preset['preset_version'], capture=asdict(observation), devices=[device])


def identity_result(record, candidates, status, capabilities):
    return dict(observation=deepcopy(record),
                identity=dict(status=status, candidates=deepcopy(candidates), selected=None),
                capabilities=dict(protocol=None, driver=None, support='unchecked',
                                  hardware_qualified=capabilities.hardware_qualified),
                recommendation=dict(status='awaiting_identity', settings={}, source='catalogue', automatic_apply=False),
                operating_status='not_configured')


def confirm_identity(device, capabilities, catalogue):
    """Called only after an explicit confirmation bound to the current report."""
    identity = device['identity']
    require(len(identity['candidates']) == 1 and
            (identity['status'] == 'candidate' or device['observation'].get('method') == 'manual'),
            'A single candidate or explicit manual selection is required')
    ref = identity['candidates'][0]
    descriptor = catalogue.descriptors[(ref['id'], ref['version'])]
    identity.update(status='user_confirmed', selected=deepcopy(ref))
    supported = descriptor['driver_id'] in capabilities.drivers
    device['capabilities'].update(protocol=descriptor['protocol'], driver=descriptor['driver_id'],
                                  support='available' if supported else 'unavailable')
    device['recommendation']['status'] = 'awaiting_review' if supported else 'blocked'


def identify_i2c(observation, capabilities, *, catalogue=None, policy=None):
    """Return evidence plus catalogue specifics, never a deployable node profile.

    The caller must retain only observations from its current exclusive USB
    session. This resolver does not turn saved files into current evidence.
    Policy injection is for trusted callers/tests, never a browser request.
    """
    catalogue = catalogue or Catalogue()
    policy = load_document(ROOT / "catalogue/discovery.json") if policy is None else policy
    check(policy, load_document(ROOT / "schemas/discovery-catalogue.schema.json"), "discovery")
    preset = load_document(ROOT / "catalogue/connections/bench-i2c.json")
    require(observation.preset_code == preset["wire_code"], "Unknown connection preset")
    require(capabilities.board == preset["board"]["variant"], "Discovery board mismatch")
    require(capabilities.features & 2, "Firmware does not advertise discovery")
    entries = []
    seen = set()
    for entry in policy["entries"]:
        ref = entry["descriptor"]
        key = (ref["id"], ref["version"])
        require(key not in seen and key in catalogue.descriptors, "Invalid discovery descriptor reference")
        seen.add(key)
        descriptor = catalogue.descriptors[key]
        rule = entry["identification"]
        if rule["method"] == "i2c_chip_id":
            require(descriptor["protocol"] == "i2c", "Identification protocol mismatch")
            address_schema = descriptor["instance_schema"]["properties"]["interface"]["properties"]["address"]
            for address in rule["addresses"]:
                check(f"0x{address:02x}", address_schema, "discovery.address")
            entries.append((entry, descriptor))
    results = []
    for record in observation.records:
        matches = []
        if observation.result == "complete" and record["status"] == "responding":
            for entry, descriptor in entries:
                rule = entry["identification"]
                if (record["address"] in rule["addresses"] and
                    record["register"] == rule["register"] and record["value"] == rule["value"]):
                    matches.append((entry, descriptor))
        require(len(matches) <= 8, "Discovery candidate limit exceeded; review catalogue")
        identity = "unknown" if not matches else "verified" if len(matches) == 1 else "ambiguous"
        descriptor = matches[0][1] if len(matches) == 1 else None
        supported = descriptor is not None and descriptor["driver_id"] in capabilities.drivers
        results.append({
            "observation": deepcopy(record),
            "identity": {"status": identity,
                         "candidates": [deepcopy(entry["descriptor"]) for entry, _ in matches],
                         "selected": deepcopy(matches[0][0]["descriptor"]) if descriptor else None},
            "capabilities": {"protocol": descriptor["protocol"] if descriptor else None,
                             "driver": descriptor["driver_id"] if descriptor else None,
                             "support": "unchecked" if descriptor is None else "available" if supported else "unavailable",
                             "hardware_qualified": capabilities.hardware_qualified},
            "recommendation": {"status": "awaiting_identity" if descriptor is None else
                                          "awaiting_review" if supported else "blocked",
                               "settings": {}, "source": "catalogue", "automatic_apply": False},
            "operating_status": "not_configured",
        })
    return {"contract_version": "0.1.0", "catalogue_version": policy["catalogue_version"],
            "board": deepcopy(preset["board"]), "preset_id": preset["preset_id"],
            "preset_version": preset["preset_version"], "capture": asdict(observation),
            "devices": results}
