"""Host-side reference model for bounded wireless stack ownership.

This model does not allocate ESP32-C3 RAM.
Production integration requires a separate C implementation and RAM approval.
"""

from dataclasses import dataclass

# Wi-Fi is the larger isolated profile. ESP-IDF 6.1 already includes its
# configured libc allowance in these byte requests.
STACK_SIZES = (2816, 3584, 4096, 6656)


@dataclass
class StackSlot:
    capacity: int
    owner: str | None = None
    generation: int = 0


class FixedStackPool:
    def __init__(self, capacities=STACK_SIZES):
        self.slots = [StackSlot(size) for size in capacities]

    def acquire(self, owner, required_bytes):
        if not owner or required_bytes <= 0:
            return None

        if any(slot.owner == owner for slot in self.slots):
            return None

        candidates = [
            (i, slot)
            for i, slot in enumerate(self.slots)
            if slot.owner is None and slot.capacity >= required_bytes
        ]

        if not candidates:
            return None

        index, slot = min(
            candidates,
            key=lambda item: (item[1].capacity, item[0]),
        )

        slot.owner = owner
        slot.generation += 1

        return (index, slot.generation)

    def release(self, token, owner, kernel_task_deleted=False):
        if not kernel_task_deleted:
            return False

        if token is None or not owner:
            return False

        index, generation = token

        if not isinstance(index, int):
            return False

        if not 0 <= index < len(self.slots):
            return False

        slot = self.slots[index]

        if slot.generation != generation or slot.owner != owner:
            return False

        slot.owner = None
        return True

    def used_bytes(self):
        return sum(
            slot.capacity
            for slot in self.slots
            if slot.owner is not None
        )

    def reserved_bytes(self):
        return sum(slot.capacity for slot in self.slots)
