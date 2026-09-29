"""Run the local configurator; connecting and deploying are explicit UI actions."""
import argparse
from .web import serve

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080, help="Local HTTP port")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    serve(args.port)

if __name__ == "__main__":
    main()
