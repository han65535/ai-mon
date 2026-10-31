#!/usr/bin/env python3
import json
import os
import sys
import time
mode = os.path.basename(sys.argv[0])
if mode == 'bad':
    print('not json', flush=True)
    sys.exit()
if mode == 'hang':
    time.sleep(30)
    sys.exit()
first = json.loads(sys.stdin.readline())
if mode == 'codex':
    assert first['method'] == 'initialize'
    print(json.dumps({'id': 1, 'result': {}}), flush=True)
    assert json.loads(sys.stdin.readline())['method'] == 'initialized'
    assert json.loads(sys.stdin.readline())['method'] == 'account/rateLimits/read'
    print(json.dumps({'id': 2, 'result': {'rateLimits': {'limitId': 'codex', 'primary': {'usedPercent': 25, 'windowDurationMins': 300}, 'secondary': None}}}), flush=True)
else:
    assert first['request']['subtype'] == 'initialize'
    print(json.dumps({'type': 'control_response', 'response': {'request_id': 'init', 'subtype': 'success'}}), flush=True)
    assert json.loads(sys.stdin.readline())['request']['subtype'] == 'get_usage'
    usage = {'rate_limits_available': False, 'rate_limits': None} if mode == 'claude-unavailable' else {'rate_limits_available': True, 'rate_limits': {'five_hour': {'utilization': 40}}}
    print(json.dumps({'type': 'control_response', 'response': {'request_id': 'quota', 'subtype': 'success', 'response': usage}}), flush=True)
