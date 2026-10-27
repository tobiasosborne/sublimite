"""Fixed fixture for tools/design_shots.sh."""
import sys


def mean(values):
    return sum(values) / len(values) if values else 0.0


if __name__ == "__main__":
    print(mean([float(a) for a in sys.argv[1:]]))
