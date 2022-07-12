FROM python:3.10.5-slim-bullseye@sha256:4c96c013c385c438a7f943161c536e1c374140ce6710af0b34513b8f83d8b7d1

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt
RUN mkdir /code

ENTRYPOINT ["python3", "/code/service.py"]
