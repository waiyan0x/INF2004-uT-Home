"""Compile a reviewed Markdown operation to the current TJ transaction rule."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from .client import validate

NAME = re.compile(r"[A-Za-z0-9_-]{1,31}")
BLOCK = re.compile(r"^```peripheral-json\r?\n(.*?)^```[ \t]*\r?$", re.M | re.S)


def exact(obj, keys, context):
    if not isinstance(obj, dict) or set(obj) != set(keys.split()):
        raise ValueError(f"{context}: unexpected or missing fields")


def integer(value, low, high, context):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{context}: expected integer {low}..{high}")


def unique_object(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise ValueError(f"duplicate JSON key: {key}")
        obj[key] = value
    return obj


def validate_description(obj):
    exact(obj, "schema_version device bus addresses source operations", "description")
    if obj["schema_version"] != "1.0.0":
        raise ValueError("unsupported description schema_version")
    if not isinstance(obj["device"], str) or not NAME.fullmatch(obj["device"]):
        raise ValueError("invalid device identifier")
    if obj["bus"] not in ("i2c", "uart"):
        raise ValueError("unsupported description bus")
    addresses = obj["addresses"]
    if not isinstance(addresses, list):
        raise ValueError("addresses must be a list")
    for address in addresses:
        integer(address, 8, 119, "seven-bit address")
    if len(set(addresses)) != len(addresses):
        raise ValueError("duplicate address")
    if (obj["bus"] == "i2c" and not addresses) or (obj["bus"] == "uart" and addresses):
        raise ValueError("I2C needs address choices; UART has no I2C addresses")
    source = obj["source"]
    exact(source, "file sha256 page_count", "source")
    if (not isinstance(source["file"], str) or
            not re.fullmatch(r"[^/\\:]+\.pdf", source["file"], re.I)):
        raise ValueError("source.file must be a PDF basename")
    if not isinstance(source["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", source["sha256"]):
        raise ValueError("source.sha256 must be lowercase SHA-256")
    integer(source["page_count"], 1, 4096, "source.page_count")
    operations = obj["operations"]
    if not isinstance(operations, dict) or not 1 <= len(operations) <= 16:
        raise ValueError("expected 1..16 named operations")
    for name, op in operations.items():
        if not isinstance(name, str) or not NAME.fullmatch(name) or not isinstance(op, dict):
            raise ValueError("invalid operation")
        kind = op.get("kind")
        if kind == "i2c_register_read":
            exact(op, "kind register read_length pages notes", name)
            if obj["bus"] != "i2c":
                raise ValueError("register read requires I2C")
            integer(op["register"], 0, 255, "register")
            integer(op["read_length"], 1, 16, "description read_length")
        elif kind == "unsupported":
            exact(op, "kind reason pages notes", name)
            if not isinstance(op["reason"], str) or not op["reason"].strip():
                raise ValueError("unsupported operation requires a reason")
        else:
            raise ValueError(f"{name}: unsupported operation kind")
        if not isinstance(op["notes"], str) or not op["notes"].strip():
            raise ValueError(f"{name}: notes are required")
        pages = op["pages"]
        if not isinstance(pages, list) or not pages:
            raise ValueError(f"{name}: source pages are required")
        for page in pages:
            integer(page, 1, source["page_count"], "PDF page")
        if len(set(pages)) != len(pages):
            raise ValueError("duplicate source page")
    return obj


def load_description(path):
    with Path(path).open("rb") as stream:
        raw = stream.read(65537)
    if len(raw) > 65536:
        raise ValueError("description exceeds 64 KiB")
    text = raw.decode("utf-8")
    blocks = BLOCK.findall(text)
    if len(blocks) != 1 or len(re.findall(r"^```peripheral-json\b", text, re.M)) != 1:
        raise ValueError("expected exactly one closed peripheral-json block")
    obj = json.loads(blocks[0], object_pairs_hook=unique_object)
    return validate_description(obj), hashlib.sha256(raw).hexdigest()


def compile_rule(description, operation, revision, address=None):
    """Pure selection: no PDF extraction, inference service, I/O or name cases."""
    obj = validate_description(description)
    if operation not in obj["operations"]:
        raise ValueError(f"unknown operation: {operation}")
    op = obj["operations"][operation]
    if op["kind"] == "unsupported":
        raise ValueError(f"{operation} is unsupported: {op['reason']}")
    if address is None:
        if len(obj["addresses"]) != 1:
            raise ValueError("select --address explicitly for this peripheral")
        address = obj["addresses"][0]
    integer(address, 8, 119, "seven-bit address")
    if address not in obj["addresses"]:
        raise ValueError("address is not a documented choice for this peripheral")
    return validate(dict(version=2, revision=revision, device=obj["device"],
                         protocol="i2c", address=address, mode=0,
                         dc_pin=255, dc_level=0, cs_pin=255,
                         rate=100000, timeout_ms=100,
                         write_hex=f"{op['register']:02X}",
                         read_length=op["read_length"]))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("description", type=Path)
    parser.add_argument("--list", action="store_true", help="show available and unsupported operations")
    parser.add_argument("--operation")
    parser.add_argument("--revision", type=int)
    parser.add_argument("--address", type=lambda text: int(text, 0), help="seven-bit decimal or 0x address")
    parser.add_argument("--output", type=Path, help="generated rule JSON; use an ignored build directory")
    parser.add_argument("--datasheets", type=Path, help="also verify the source PDF against its recorded hash")
    args = parser.parse_args(argv)
    try:
        obj, digest = load_description(args.description)
        verified = False
        if args.datasheets:
            actual = hashlib.sha256((args.datasheets / obj["source"]["file"]).read_bytes()).hexdigest()
            if actual != obj["source"]["sha256"]:
                raise ValueError("source PDF hash mismatch; review the description again")
            verified = True
        if args.list:
            print(json.dumps(obj, indent=2))
            return 0
        if args.operation is None or args.revision is None or args.output is None:
            raise ValueError("compilation requires --operation, --revision and --output")
        rule = compile_rule(obj, args.operation, args.revision, args.address)
        report = dict(rule=rule, transaction=dict(write_hex=rule["write_hex"],
                      write_length=1, read_length=rule["read_length"],
                      sequence="register write, repeated START, read, STOP"),
                      provenance=dict(description_sha256=digest, source=obj["source"],
                      source_pdf_verified=verified, operation=args.operation,
                      pages=obj["operations"][args.operation]["pages"]),
                      notes=obj["operations"][args.operation]["notes"],
                      hardware_executed=False)
        receipt = args.output.with_suffix(".report.json")
        if args.description.resolve() in (args.output.resolve(), receipt.resolve()):
            raise ValueError("output must not overwrite the input description")
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(rule, indent=2)+"\n", encoding="utf-8")
        receipt.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
    except (ValueError, OSError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
