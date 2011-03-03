
import inspect
from string import ascii_letters as alphas, digits as nums, hexdigits as hexnums, printable as printables, lower, upper

from bdec import DecodeError
from bdec.choice import Choice
from bdec.constraints import Equals, NotEquals
from bdec.data import Data
from bdec.entry import is_hidden
from bdec.expression import LengthResult, ValueResult
from bdec.field import Field
from bdec.sequence import Sequence
from bdec.sequenceof import SequenceOf
from bdec.spec.references import ReferencedEntry

alphanums = alphas + nums

class ParseException(DecodeError):
    def __init__(self, filename, text, offset, ex):
        DecodeError.__init__(self, ex.entry)
        self._filename = filename
        self._text = text
        self._offset = offset
        self.error = ex

    @property
    def _lines(self):
        return self._text[:self._offset / 8].splitlines() or ['']

    @property
    def lineno(self):
        return len(self._lines)

    @property
    def col(self):
        return len(self._lines[-1])

    def __str__(self):
        return '%s[%i]: %s\n%s\n%s' % (self._filename, self.lineno,
                str(self.error), self._text.splitlines()[self.lineno-1],
                ' ' * self.col + '^')


class ParserElement:
    def __init__(self):
        self._actions = []
        self._internal_actions = []
        self._am_resolving = False

        # The parser includes leading whitespace, the decoder doesn't.
        self._parser = None
        self._decoder = None
        self._ignore = None
        self._name = None

    def _is_important(self):
        return self._actions or self._internal_actions or self._ignore

    def ignore(self, expr):
        self._ignore = expr

    def _createEntry(self, separator):
        raise NotImplementedError()

    def createDecoder(self, separator):

        if self._decoder is not None:
            return self._decoder

        if self._am_resolving:
            self._references.append(ReferencedEntry('forward', 'forward'))
            return self._references[-1]

        if self._ignore is not None:
            separator = separator | self._ignore

        # It is possible (even likely for Forward elements) that we will be
        # referenced while creating the referencing decoder; we handle this
        # by returning ReferencedEntry instances until the decoder has been
        # constructed, then resolve them all.
        self._am_resolving = True
        self._references = []
        self._decoder = self._createEntry(separator)
        if self._name is not None:
            self._decoder.name = self._name
        for reference in self._references:
            reference.resolve(self._decoder)

        try:
            self._decoder.actions
        except AttributeError:
            self._decoder.actions = []
        self._decoder.actions += self._internal_actions + self._actions
        return self._decoder

    def setParseAction(self, fn):
        self._actions = []
        return self.addParseAction(fn)

    def addParseAction(self, fn):
        num_args = len(inspect.getargspec(fn)[0])
        if num_args == 3:
            action = lambda t:fn('', 0, t)
        elif num_args == 2:
            action = lambda t:fn(0, t)
        else:
            action = fn

        self._actions.append(action)
        return self

    def parseString(self, text):
        if isinstance(text, unicode):
            text = text.encode('ascii')

        stack = []
        tokens = []
        for is_starting, name, entry, data, value in self._decode(text, '<string>'):
            if is_starting:
                stack.append(tokens)
                tokens = []
            else:
                if name and not is_hidden(name) and value is not None:
                    tokens.append(value)

                try:
                    actions = entry.actions
                except AttributeError:
                    actions = []

                for action in actions:
                    tokens = action(tokens)
                    if not isinstance(tokens, list):
                        tokens = [tokens]

                # Extend the current tokens list with the child tokens
                stack[-1].extend(tokens)
                tokens = stack.pop()
        assert len(stack) == 0
        return tokens

    def _decode(self, text, filename):
        if self._parser is None:
            whitespace = (Literal(' ') | '\n')('whitespace')
            if self._ignore is not None:
                whitespace = whitespace | self._ignore
            whitespace = Suppress(ZeroOrMore(whitespace))

            # Whitespace is decoded at the end of the Literal (and Word) entries,
            # so we have to decode any leading whitespace. The alternative,
            # decoding the whitespace before Literal (and Word) entries, would
            # prevent the Chooser from being able to guess the type.
            self._parser = Sequence(None, [whitespace.createDecoder(None),
                self.createDecoder(whitespace)])

        offset = 0
        try:
            for is_starting, name, entry, data, value in self._parser.decode(Data(text)):
                if not is_starting:
                    offset += len(data)
                yield is_starting, name, entry, data, value
        except DecodeError, ex:
            raise ParseException(filename, text, offset, ex)

    def __add__(self, other):
        if not isinstance(other, ParserElement):
            other = Literal(other)
        return And([self, other])

    def __radd__(self, other):
        if not isinstance(other, ParserElement):
            other = Literal(other)
        return And([other, self])

    def __or__(self, other):
        return MatchFirst([self, other])

    def __ror__(self, other):
        if not isinstance(other, ParserElement):
            other = Literal(other)
        return MatchFirst([other, self])

    def __invert__(self):
        return NotAny(self)

    def __call__(self, name):
        return self.setName(name)

    def setName(self, name):
        self._name = name
        return self


class ZeroOrMore(ParserElement):
    def __init__(self, element):
        ParserElement.__init__(self)
        self.element = element

    def _createEntry(self, separator):
        entry = self.element.createDecoder(separator)
        end = Sequence(None, [])
        item = Choice('item', [entry, end])
        return SequenceOf('items', item, end_entries=[end])

    def __str__(self):
        return '{%s}' % self.element


def OneOrMore(element):
    return element + ZeroOrMore(element)


class Literal(ParserElement):
    def __init__(self, text):
        ParserElement.__init__(self)
        assert isinstance(text, basestring), 'Literal must be a string! Is %s' % (repr(text))
        self.text = text

    def _createEntry(self, separator):
        result = Field('literal', length=len(self.text) * 8, format=Field.TEXT, constraints=[Equals(self.text)])
        if separator:
            result = Sequence('literal', [result, separator.createDecoder(None)])
        return result

    def __str__(self):
        return '"%s"' % repr(self.text)[1:-1]


def Word(init_chars, body_chars=None):
    init = MatchFirst([Literal(c) for c in init_chars])
    if body_chars:
        body = MatchFirst([Literal(c) for c in body_chars])
        result = init + ZeroOrMore(body)
    else:
        result = OneOrMore(init)
    return Combine(result)('word')


def _check_literals(exprs):
    result = []
    for expr in exprs:
        if not isinstance(expr, ParserElement):
            result.append(Literal(expr))
        else:
            result.append(expr)
    return result

class And(ParserElement):
    def __init__(self, exprs):
        ParserElement.__init__(self)
        self.exprs = _check_literals(exprs)

    def _createEntry(self, separator):
        return Sequence('and', [e.createDecoder(separator) for e in self.exprs])

    def __add__(self, other):
        if not isinstance(other, ParserElement):
            other = Literal(other)

        if self._is_important():
            return ParserElement.__add__(self, other)
        return And(self.exprs + [other])

    def __radd__(self, other):
        if self._is_important():
            return ParserElement.__radd__(self, other)
        return And([Literal(other)] + self.exprs)

    def __str__(self):
        return '(%s)' % ', '.join(str(e) for e in self.exprs)


class MatchFirst(ParserElement):
    def __init__(self, exprs):
        ParserElement.__init__(self)
        self.exprs = _check_literals(exprs)

    def _createEntry(self, separator):
        return Choice('or', [e.createDecoder(separator) for e in self.exprs])

    def __str__(self):
        return '[%s]' % (', '.join(str(e) for e in self.exprs))

    def __or__(self, other):
        if not isinstance(other, ParserElement):
            other = Literal(other)

        if self._is_important():
            return ParserElement.__or__(self, other)
        return MatchFirst(self.exprs + [other])

    def __ror__(self, other):
        if self._is_important():
            return ParserElement.__ror__(self, other)
        return MatchFirst([Literal(other)] + self.exprs)

# In pyparsing this means 'match the longest entry'; we don't do that, so
# just pretend it's the same as MatchFirst.
Or = MatchFirst

class StringEnd(ParserElement):
    def _createEntry(self, separator):
        data = Choice('data:', [Field(None, length=8), Sequence(None, [])])
        length_check = Sequence(None, [], value=LengthResult('data:'), constraints=[Equals(0)])
        return Sequence(None, [data, length_check])


class NoMatch(ParserElement):
    def _createEntry(self, separator):
        return Sequence(None, [], value=Constant(0), constraints=[Equals(1)])


class CharsNotIn(ParserElement):
    def __init__(self, notChars):
        ParserElement.__init__(self)
        self.notChars = notChars

    def _createEntry(self, separator):
        checks = []
        for c in self.notChars:
            checks.append(Sequence(None, [], value=ValueResult('char not in'),
                    constraints=[NotEquals(ord(c))]))
        good_char = Sequence('good char', [Field('char not in', length=8, format=Field.INTEGER)] + checks)
        end = Sequence(None, [])
        char = Choice('test char', [good_char, end])
        entry = SequenceOf('chars not in', char, end_entries=[end])

        def joinCharacters(toks):
            if toks:
                return [''.join(chr(t) for t in toks)]
            return []
        self._internal_actions.append(joinCharacters)

        children = [good_char, entry]
        if separator:
            children.append(separator.createDecoder(None))
        result = Sequence('chars not in', children)
        return result

    def __str__(self):
        return '[not in %s]' % repr(self.notChars)


def Suppress(expr):
    if not isinstance(expr, ParserElement):
        expr = Literal(expr)
    return expr.setParseAction(lambda t:[])


class Forward(ParserElement):
    def __init__(self):
        ParserElement.__init__(self)
        self.element = None

    def __lshift__(self, expr):
        if not isinstance(expr, ParserElement):
            expr = Literal(expr)
        self.element = expr

    def _createEntry(self, separator):
        assert self.element is not None
        return self.element.createDecoder(separator)


class NotAny(ParserElement):
    def __init__(self, expr):
        ParserElement.__init__(self)

        if not isinstance(expr, ParserElement):
            expr = Literal(expr)
        self.expr = expr

    def _createEntry(self, separator):
        # This entry should _not_ decode if self.expr is present
        entry = self.expr.createDecoder(separator)
        null = Sequence('null', [])
        is_present = Choice('is present', [entry, null])
        check = Sequence('check:', [], value=LengthResult('is present'), constraints=[Equals(0)])
        return Sequence('not any:', [is_present, check])


class SkipTo(ParserElement):
    def __init__(self, expr):
        ParserElement.__init__(self)

        if not isinstance(expr, ParserElement):
            expr = Literal(expr)
        self.expr = expr

    def _createEntry(self, separator):
        end = self.expr.createDecoder(separator)
        return SequenceOf('skip to', Choice('item', [end, Field('skipped', 8)]), end_entries=[end])


class Combine(ParserElement):
    def __init__(self, expr):
        ParserElement.__init__(self)
        self.expr = expr

        def joinTokens(toks):
            if toks:
                return [''.join(t for t in toks)]
            return []
        self._internal_actions.append(joinTokens)

    def _createEntry(self, separator):
        result = self.expr.createDecoder(None)
        if separator is not None:
            result = Sequence('combine', [result, separator.createDecoder(None)])
        return result

def srange(text):
    assert text[0] == '[' and text[-1] == ']'
    text = text[1:-1]
    chars = ''
    while text:
        if len(text) > 2 and text[1] == '-':
            chars += ''.join(chr(i) for i in range(ord(text[0]), ord(text[2])+1))
            text = text[3:]
        else:
            chars += text[0]
            text = text[1:]
    return chars

def _get_caseless_action(text):
    if text[0].isupper():
        action = upper
    else:
        action = lower
    return lambda t:action(t[0])

def CaselessLiteral(text):
    chars = ((Literal(c.lower()) | Literal(c.upper())) for c in text)
    element = Combine(reduce(lambda a,b:a+b, chars))
    return element.setParseAction(_get_caseless_action(text))

restOfLine = CharsNotIn('\n') + '\n'

def Optional(expr, null=''):
    return expr | Literal(null)

def QuotedString(quoteChar, multiline=True, escChar=''):
    return nestedExpr(quoteChar, quoteChar)

def nestedExpr(opener='(', closer=')'):
    result = Forward()
    result << opener + ZeroOrMore(result | CharsNotIn(opener + closer)) + closer
    return result

def keepOriginalText(s, l, t):
    # TODO: This isn't quite right...
    return ''.join(t)

def Group(expr):
    return And([expr]).addParseAction(lambda t:[t])

def CaselessKeyword(text):
    return Combine(And(list(Or([c.lower(), c.upper()]) for c in text)))

