# dscoop-relay

Релей/хаб DSCoop: коды приглашений + ретрансляция UDP для игроков за NAT. Один файл на
Python 3.10+, без зависимостей. Подробности протокола и схемы — `../docs/relay.md`.

```bash
python3 relay.py --control-port 27000 --port-range 27100-27999
docker compose up -d --build        # то же в контейнере (host network)
pip install -r requirements-dev.txt && pytest
```
Открой UDP 27000 и 27100–27999. Клиенты указывают хост релея в `dscoop/dscoop.json`.
