FROM python:3.11.0b5-slim-bullseye@sha256:d88fe6006125808c28ad7b0ed4fb01b754fe13a85de50f8a41c1bf145f7f8295

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
