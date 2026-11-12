"""Compatibility entry point: the experiment collector now lives in host/collect.py."""
from host.collect import main

if __name__ == '__main__':
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        print('\nPaused. Run the same command to resume accepted trials.')
