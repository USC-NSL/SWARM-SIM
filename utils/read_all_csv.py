import os
import argparse


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('dir')
    parser.add_argument('prefix')
    parser.add_argument('count')

    first_line = None
    prefix = None
    results = []

    args = parser.parse_args()
    for i in range(int(args.count)):
        name = args.prefix + '-' + str(i) + '.csv'
        path = os.path.join(args.dir, name)
        with open(path, 'r') as csvfile:
            if not first_line:
                first_line = csvfile.readline().strip()
                continue
            else:
                line = csvfile.readlines()[-1].strip()
                fields = line.split(',')
                if prefix is None:
                    prefix = ','.join(fields[0:-1])
                results.append(fields[-1])

    print(prefix + ',' + ','.join(results))
                
