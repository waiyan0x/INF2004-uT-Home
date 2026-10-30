from fixed_stack_pool import FixedStackPool

pool = FixedStackPool()

for round_number in range(1000):
    allocations = []

    for owner, size in [
        ("timer", 5632),
        ("tcpip", 5120),
        ("ble", 6144),
    ]:
        token = pool.acquire(owner, size)
        assert token is not None
        allocations.append((owner, token))

    assert pool.acquire("extra", 1024) is None

    for owner, token in allocations:
        assert pool.release(
            token,
            owner,
            kernel_task_deleted=True
        )

    assert pool.used_bytes() == 0

print("PASS: 1000 allocation/release rounds")
print("PASS: No slot leaks in host reference model")
