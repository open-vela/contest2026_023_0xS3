.PHONY: check test

check: test
	PYTHONPATH=src python3 -m compileall -q src tests
	python3 -m json.tool config.example.json >/dev/null

test:
	PYTHONPATH=src python3 -m unittest discover -s tests -p 'test_*.py' -v
