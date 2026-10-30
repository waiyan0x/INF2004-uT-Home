import unittest

try:
    from fixed_stack_pool import FixedStackPool
except ModuleNotFoundError:
    from tests.buddy1.fixed_stack_pool import FixedStackPool


class FixedStackPoolTests(unittest.TestCase):

    def test_three_tasks_acquire_exclusive_stacks(self):
        pool = FixedStackPool()

        event = pool.acquire("event", 2816)
        tcpip = pool.acquire("tcpip", 3584)
        timer = pool.acquire("timer", 4096)
        wifi = pool.acquire("wifi", 6656)

        self.assertIsNotNone(event)
        self.assertIsNotNone(timer)
        self.assertIsNotNone(tcpip)
        self.assertIsNotNone(wifi)

        self.assertEqual(len({event[0], tcpip[0], timer[0], wifi[0]}), 4)
        self.assertEqual(pool.used_bytes(), 17152)

    def test_exhaustion_rejected(self):
        pool = FixedStackPool()

        for name, size in (
            ("event", 2816),
            ("tcpip", 3584),
            ("timer", 4096),
            ("wifi", 6656),
        ):
            self.assertIsNotNone(pool.acquire(name, size))

        self.assertIsNone(pool.acquire("extra", 2048))

    def test_oversized_request_rejected(self):
        pool = FixedStackPool()
        self.assertIsNone(pool.acquire("oversized", 10000))

    def test_running_task_cannot_release(self):
        pool = FixedStackPool()
        token = pool.acquire("timer", 4096)

        self.assertFalse(
            pool.release(token, "timer", kernel_task_deleted=False)
        )
        self.assertEqual(pool.used_bytes(), 4096)

    def test_verified_deleted_task_can_release(self):
        pool = FixedStackPool()
        token = pool.acquire("timer", 4096)

        self.assertTrue(
            pool.release(token, "timer", kernel_task_deleted=True)
        )
        self.assertEqual(pool.used_bytes(), 0)

    def test_stale_handle_cannot_release_new_owner(self):
        pool = FixedStackPool()

        old_token = pool.acquire("timer", 4096)
        self.assertTrue(
            pool.release(
                old_token, "timer", kernel_task_deleted=True
            )
        )

        new_token = pool.acquire("new_timer", 4096)

        self.assertFalse(
            pool.release(
                old_token, "timer", kernel_task_deleted=True
            )
        )

        self.assertNotEqual(old_token, new_token)
        self.assertEqual(pool.used_bytes(), 4096)

    def test_duplicate_owner_rejected(self):
        pool = FixedStackPool()

        self.assertIsNotNone(pool.acquire("timer", 4096))
        self.assertIsNone(pool.acquire("timer", 3584))


if __name__ == "__main__":
    unittest.main(verbosity=2)
