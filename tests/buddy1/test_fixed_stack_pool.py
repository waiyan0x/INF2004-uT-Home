import unittest

try:
    from fixed_stack_pool import FixedStackPool
except ModuleNotFoundError:
    from tests.buddy1.fixed_stack_pool import FixedStackPool


class FixedStackPoolTests(unittest.TestCase):

    def test_three_tasks_acquire_exclusive_stacks(self):
        pool = FixedStackPool()

        timer = pool.acquire("timer", 5632)
        tcpip = pool.acquire("tcpip", 5120)
        ble = pool.acquire("ble", 6144)

        self.assertIsNotNone(timer)
        self.assertIsNotNone(tcpip)
        self.assertIsNotNone(ble)

        self.assertEqual(len({timer[0], tcpip[0], ble[0]}), 3)
        self.assertEqual(pool.used_bytes(), 16896)

    def test_exhaustion_rejected(self):
        pool = FixedStackPool()

        for name, size in (
            ("timer", 5632),
            ("tcpip", 5120),
            ("ble", 6144),
        ):
            self.assertIsNotNone(pool.acquire(name, size))

        self.assertIsNone(pool.acquire("extra", 2048))

    def test_oversized_request_rejected(self):
        pool = FixedStackPool()
        self.assertIsNone(pool.acquire("oversized", 10000))

    def test_running_task_cannot_release(self):
        pool = FixedStackPool()
        token = pool.acquire("timer", 5632)

        self.assertFalse(
            pool.release(token, "timer", kernel_task_deleted=False)
        )
        self.assertEqual(pool.used_bytes(), 5632)

    def test_verified_deleted_task_can_release(self):
        pool = FixedStackPool()
        token = pool.acquire("timer", 5632)

        self.assertTrue(
            pool.release(token, "timer", kernel_task_deleted=True)
        )
        self.assertEqual(pool.used_bytes(), 0)

    def test_stale_handle_cannot_release_new_owner(self):
        pool = FixedStackPool()

        old_token = pool.acquire("timer", 5632)
        self.assertTrue(
            pool.release(
                old_token, "timer", kernel_task_deleted=True
            )
        )

        new_token = pool.acquire("new_timer", 5632)

        self.assertFalse(
            pool.release(
                old_token, "timer", kernel_task_deleted=True
            )
        )

        self.assertNotEqual(old_token, new_token)
        self.assertEqual(pool.used_bytes(), 5632)

    def test_duplicate_owner_rejected(self):
        pool = FixedStackPool()

        self.assertIsNotNone(pool.acquire("timer", 5632))
        self.assertIsNone(pool.acquire("timer", 5120))


if __name__ == "__main__":
    unittest.main(verbosity=2)
