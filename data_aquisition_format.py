import json
import time
import hmac
import hashlib
import os
import configparser

config = configparser.ConfigParser()
config.read('config.ini')

HMAC_KEY = config['edge_impulse']['hmac_key']
print(HMAC_KEY)

dir = 'raw_data'

for filename in os.listdir(dir):
    if filename.endswith('.csv'):
        prefix, ext = os.path.splitext(filename)
        outfilename = os.path.join('formatted_data', '{}.json'
                         .format(prefix))

        values = []
        with open(os.path.join(dir, filename)) as fp:
            for line in fp:
                line = line.strip()
                values.append([int(i) for i in line.split(',')])
                emptySignature = ''.join(['0'] * 64)
                data = {
                    "protected": {
                        "ver": "v1",
                        "alg": "HS256",
                        "iat": time.time()
                    },
                    "signature": emptySignature,
                    "payload": {
                        "device_name": "E1:01:2D:53:40:DA",
                        "device_type": "NRF5340DK",
                        "interval_ms": 40,
                        "sensors": [
                            { "name": "accX", "units": "m/s2" },
                            { "name": "accY", "units": "m/s2" },
                            { "name": "accZ", "units": "m/s2" }
                        ],
                        "values": values
                    }
                }

                # encode in JSON
                encoded = json.dumps(data)

                # sign message
                signature = hmac.new(bytes(HMAC_KEY, 'utf-8'),
                                     msg = encoded.encode('utf-8'), 
                                     digestmod = hashlib.sha256).hexdigest()

                # set the signature again in the message, and encode again
                data['signature'] = signature
                encoded = json.dumps(data, indent=4)
                with open(outfilename, 'w') as fout:
                    fout.write(encoded)

