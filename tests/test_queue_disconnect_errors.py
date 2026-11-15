"""Verify queue tests recognize aborted sockets without masking timeouts."""

import unittest
from unittest.mock import Mock, patch

import test_connection_queue as queue


class QueueDisconnectErrorTests(unittest.TestCase):
    # Simulate Windows abort while sending incomplete request body
    def test_aborted_send_counts_as_closed(self):
        slow = Mock()
        slow.send.side_effect = [None, ConnectionAbortedError("aborted")]
        prompt = Mock()
        prompt.recv.return_value = b"HTTP/1.1 200 OK\r\n"
        case = queue.ConnectionQueueTests("test_slow_request_body_deadline_disconnects")
        with patch.object(case, "write_conf", return_value="unused"), \
                patch.object(case, "start_server"), \
                patch.object(queue.socket, "socket", side_effect=[slow, prompt]), \
                patch.object(queue.time, "sleep"):
            case.test_slow_request_body_deadline_disconnects()

    # Simulate Windows abort while reading after slow transfer
    def test_aborted_receive_counts_as_closed(self):
        sock = Mock()
        sock.recv.side_effect = ConnectionAbortedError("aborted")
        case = queue.ConnectionQueueTests("test_slow_request_body_minimum_rate_disconnects")
        with patch.object(case, "write_conf", return_value="unused"), \
                patch.object(case, "start_server"), \
                patch.object(queue.socket, "socket", return_value=sock), \
                patch.object(queue.time, "sleep"):
            case.test_slow_request_body_minimum_rate_disconnects()

    # Keep timeout distinct from confirmed connection closure
    def test_receive_timeout_fails_closure_assertion(self):
        sock = Mock()
        sock.recv.side_effect = TimeoutError("still open")
        case = queue.ConnectionQueueTests("test_slow_request_body_minimum_rate_disconnects")
        with patch.object(case, "write_conf", return_value="unused"), \
                patch.object(case, "start_server"), \
                patch.object(queue.socket, "socket", return_value=sock), \
                patch.object(queue.time, "sleep"):
            with self.assertRaises(AssertionError):
                case.test_slow_request_body_minimum_rate_disconnects()


if __name__ == "__main__":
    unittest.main()
