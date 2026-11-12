---
description: get_json(): fetches a URL and parses the JSON answer, using only the standard library
order: 30
section: system
---
import json
import urllib.request
from typing import Any


# Raises urllib.error.HTTPError when the server answers with an error status (e.code is 404, 500...)
# and urllib.error.URLError when there's no answer at all (no network, refused, timed out).
def get_json(url: str, timeout: float = 10.0) -> Any:
    request = urllib.request.Request(url, headers={"Accept": "application/json"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.load(response)
