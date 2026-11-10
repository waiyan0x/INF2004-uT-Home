try:
    from fixed_stack_pool import FixedStackPool
except ModuleNotFoundError:
    from tests.buddy1.fixed_stack_pool import FixedStackPool

pool = FixedStackPool()

for round_number in range(1000):
    allocations = []

    for owner, size in [
        ("event", 2816),
        ("tcpip", 3584),
        ("timer", 4096),
        ("wifi", 6656),
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
