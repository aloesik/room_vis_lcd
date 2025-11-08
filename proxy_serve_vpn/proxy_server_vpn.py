from flask import Flask, request, Response
import requests
from requests_oauthlib import OAuth1

app = Flask(__name__)

SITE_NAME = "https://apps.usos-szkol.pwr.edu.pl"

CONSUMER_KEY = "ApHWU9fCxcfjs5teb3Uj"
CONSUMER_SECRET = "YtddWP3Ap8FrWSdqet2gpKvbADpsrhZW6j2vKRey"

@app.route("/<path:path>", methods=["GET"])
def proxy(path):
    url = f"{SITE_NAME}/{path}"
    headers = {k: v for k, v in request.headers if k.lower() != "host"}

    # OAuth1 signing
    auth = OAuth1(CONSUMER_KEY, CONSUMER_SECRET, signature_method="HMAC-SHA1")

    resp = requests.get(url, params=request.args, headers=headers,
                        auth=auth, verify=False, timeout=10)

    # Save debug copy if it’s a timetable request
    if "services/tt/room" in path:
        from os.path import dirname, join
        with open(join(dirname(__file__), "schedule.json"), "wb") as f:
            f.write(resp.content)
        print(f"[INFO] schedule.json saved ({len(resp.content)} bytes)")

    return Response(resp.content, status=resp.status_code,
                    content_type=resp.headers.get("Content-Type"))

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)