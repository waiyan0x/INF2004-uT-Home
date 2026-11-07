"""Bounded generic I2C draft construction; never access a peripheral bus."""
from copy import deepcopy

from tools.config_validator.planner import ROOT, check, load_document, require
from tools.config_validator.recipe import validate_recipe_selection
from .drafts import append_instance, descriptor_instance


RECIPE_SCHEMA = load_document(ROOT / "schemas/i2c-recipe.schema.json")
RECIPE_FIELDS = tuple(RECIPE_SCHEMA["properties"])


def prepare_recipe(document, recipe, descriptor, *, replace_device_id=None):
    """Append a copied reader to an existing I2C bus without replacing owners.

    The caller validates the complete incoming and resulting profiles and
    verifies compiled firmware support. Physical wiring is user-confirmed.
    """
    check(recipe, RECIPE_SCHEMA, "recipe")
    validate_recipe_selection(recipe)
    existing = None
    if replace_device_id is None:
        require(all(device["device_id"] != recipe["device_id"] for device in document["devices"]),
                "This device name is already in the profile")
    else:
        require(isinstance(replace_device_id, str) and recipe["device_id"] == replace_device_id,
                "Editing a recipe must keep its existing device name")
        existing = next((device for device in document["devices"]
                         if device["device_id"] == replace_device_id), None)
        require(existing is not None and existing["driver"] == "i2c_recipe",
                "Choose an existing generic I2C recipe; another driver cannot be replaced here")
    buses = [bus for bus in document["interfaces"]
             if bus["id"] == recipe["bus_ref"] and bus["protocol"] == "i2c"]
    require(len(buses) == 1, "Choose an existing I2C interface; recipe preparation never changes wiring")
    for owner in document["devices"]:
        if owner is existing:
            continue
        connection = owner["interface"]
        if connection.get("ref") == recipe["bus_ref"] and connection.get("address") == recipe["address"]:
            raise ValueError(f"Remove the existing I2C owner {owner['device_id']} from the draft first, "
                             "or choose the complete i2c-recipe-demo example")
    draft = deepcopy(document)
    if existing is None:
        instance = descriptor_instance(descriptor, dict(id="i2c_recipe", version="1.0.0"),
                                       device_id=recipe["device_id"], task_name="read_" + recipe["device_id"],
                                       interface=dict(ref=recipe["bus_ref"], address=recipe["address"]))
        instance["behaviour"]["destination"] = "readings." + recipe["device_id"]
    else:
        instance = next(device for device in draft["devices"] if device["device_id"] == replace_device_id)
        instance["interface"] = dict(ref=recipe["bus_ref"], address=recipe["address"])
    for field in RECIPE_FIELDS:
        if field not in ("device_id", "bus_ref", "address"):
            instance["behaviour"][field] = recipe[field]
    if existing is None:
        append_instance(draft, instance, "raw_sample")
    return draft


def recipe_catalogue(descriptor):
    """Copy bounds and illustrative register reads for the local form."""
    limits = {key: dict(min=value["minimum"], max=value["maximum"])
              for key, value in RECIPE_SCHEMA["properties"].items() if value.get("type") == "integer"}
    limits.update(driver=descriptor["driver_id"], version=descriptor["descriptor_version"],
                  requires_existing_bus=True)
    base = dict(device_id="register_reader", bus_ref="i2c0", address="0x77", register=0,
                read_length=1, read_pause_ms=0, select_offset=0, select_length=1,
                interval_ms=1000, timeout_ms=100)
    examples = [dict(id="bmp388-chip-id", label="BMP388 chip ID", recipe=base,
                     description="Read register 0x00 from the confirmed BMP388. This does not initialize measurements.",
                     expected="0x50"),
                dict(id="bmp388-status", label="BMP388 status register", recipe=base | {"register": 3},
                     description="Read register 0x03 with the same firmware. Status bits depend on the sensor state; zero is valid.")]
    return dict(recipe_limits=limits, recipe_examples=examples)
