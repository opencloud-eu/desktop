import time
from posixpath import join


def url_join(*args):
    paths = []
    for path in list(args):
        paths.append(path.strip("/"))
    return join("", *paths)


def perform_request_with_retry(request_func, retry_condition, retries=1, interval=1):
    """
    Perform a request with retry logic.

    Args:
        request_func (callable): The function to perform the request.
        retry_condition (callable): A function that takes the response
                            and calculates whether to retry or not.
                            Should return True if the request should be retried, False otherwise.
        retries (int): The number of times to retry the request if the condition is not met.
        interval (int): The time to wait between retries in seconds.

    Returns:
        The response from the request function.
    """
    retry_count = 0
    while retry_count <= retries:
        response = request_func()
        # return early if the retry condition is not met
        if not retry_condition(response):
            return response
        retry_count += 1
        if retry_count <= retries:
            time.sleep(interval)
    return response
