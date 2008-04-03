import bdec.spec.expression as exp
import unittest

class TestExpression(unittest.TestCase):
    def test_simple_int(self):
        self.assertEqual(5, exp.compile('5'))

    def test_add(self):
        self.assertEqual(8, int(exp.compile('5 + 3')))

    def test_sub(self):
        self.assertEqual(2, int(exp.compile('8 - 6')))

    def test_compound(self):
        self.assertEqual(12, int(exp.compile('6 + 7 - 1')))

    def test_brackets(self):
        self.assertEqual(6, int(exp.compile('(6)')))
        self.assertEqual(7, int(exp.compile('(6 + 1)')))
        self.assertEqual(7, int(exp.compile('(6) + 1')))
        self.assertEqual(4, int(exp.compile('1 + (5 - 2)')))

    def test_multiply(self):
        self.assertEqual(8, int(exp.compile('4 * 2')))

    def test_divide(self):
        self.assertEqual(7, int(exp.compile('14 / 2')))

    def test_mod(self):
        self.assertEqual(2, int(exp.compile('8 % 3')))

    def test_bimdas(self):
        self.assertEqual(25, int(exp.compile('5 + 2 * 10')))
        self.assertEqual(70, int(exp.compile('(5 + 2) * 10')))

    def test_named_reference(self):
        a = exp.compile('${bob}')
        self.assertTrue(isinstance(a, exp.ValueResult))
        self.assertEquals('bob', a.name)

    def test_length_lookup(self):
        a = exp.compile('len{bob}')
        self.assertTrue(isinstance(a, exp.LengthResult))
        self.assertEquals('bob', a.name)

    def test_hex(self):
        self.assertEqual(5, int(exp.compile("0x5")))
        self.assertEqual(255, int(exp.compile("0xfF")))
