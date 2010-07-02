
import sys
import bdec
import bdec.data as dt
import bdec.spec.xmlspec as xmlspec
import bdec.output.xmlout as xmlout

def main():
    spec = sys.argv[1]
    try:
        decoder, common, lookup = xmlspec.load(spec)
    except bdec.spec.LoadError, ex:
        sys.exit(str(ex))

    if len(sys.argv) == 3:
        xml = file(sys.argv[2], 'rb').read()
    else:
        xml = sys.stdin.read()

    data = xmlout.encode(protocol, xml)
    try:
        binary = str(reduce(lambda a,b:a+b, data))
    except bdec.DecodeError, ex:
        (filename, line_number, column_number) = lookup[ex.entry]
        sys.exit("%s[%i]: %s" % (filename, line_number, str(ex)))
    sys.stdout.write(binary)
