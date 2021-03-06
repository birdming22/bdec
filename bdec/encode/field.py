#   Copyright (C) 2008-2010 Henry Ludemann
#
#   This file is part of the bdec decoder library.
#
#   The bdec decoder library is free software; you can redistribute it
#   and/or modify it under the terms of the GNU Lesser General Public
#   License as published by the Free Software Foundation; either
#   version 2.1 of the License, or (at your option) any later version.
#
#   The bdec decoder library is distributed in the hope that it will be
#   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
#   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   Lesser General Public License for more details.
#
#   You should have received a copy of the GNU Lesser General Public
#   License along with this library; if not, see
#   <http://www.gnu.org/licenses/>.
#  
# This file incorporates work covered by the following copyright and  
# permission notice:  
#  
#   Copyright (c) 2010, PRESENSE Technologies GmbH
#   All rights reserved.
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions are met:
#       * Redistributions of source code must retain the above copyright
#         notice, this list of conditions and the following disclaimer.
#       * Redistributions in binary form must reproduce the above copyright
#         notice, this list of conditions and the following disclaimer in the
#         documentation and/or other materials provided with the distribution.
#       * Neither the name of the PRESENSE Technologies GmbH nor the
#         names of its contributors may be used to endorse or promote products
#         derived from this software without specific prior written permission.
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#   DISCLAIMED. IN NO EVENT SHALL PRESENSE Technologies GmbH BE LIABLE FOR ANY
#   DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#   ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from bdec import DecodeError
from bdec.constraints import Equals
from bdec.data import Data, DataError, IntegerTooLongError
from bdec.field import Field, FieldDataError, BadFormatError, BadEncodingError
from bdec.encode.entry import EntryEncoder
from bdec.expression import UndecodedReferenceError
from bdec.inspect.param import UnknownReferenceError
from bdec.inspect.type import expression_range as erange, EntryValueType
from bdec.inspect.range import Ranges

class MissingFieldException(DecodeError):
    def __str__(self):
        return 'Unknown value when encoding %s.' % self.entry

class VariableIntegerTooLongError(DecodeError):
    def __init__(self, entry, value):
        DecodeError.__init__(self, entry)
        self.value = value

    def __str__(self):
        return '%s is too long to fit in variable length integer %s' % (self.value, self.entry)

def _convert_type(entry, data, expected_type):
    try:
        return expected_type(data)
    except:
        raise BadFormatError(entry, data, expected_type)

def get_valid_integer_lengths(entry, params):
    length_range = erange(entry.length, entry, params)
    def is_valid(length):
        if length_range.min is not None and length_range.min > length:
            return False
        if length_range.max is not None and length_range.max < length:
            return False
        return True
    possible_lengths = [8, 16, 24, 32, 64]
    return (l for l in possible_lengths if is_valid(l))

def _encode_unknown_variable_length_integer(entry, value, params):
    # Integers require a specific length of encoding. If one is
    # not specified, we'll try several lengths until we find one
    # that fits.
    #
    # We only consider lengths that are in the range specified by
    # the entry length to avoid choosing an out of bounds length.
    assert params is not None, "Asked to encode a variable length field " \
            "without parameters. This shouldn't happen."
    for length in get_valid_integer_lengths(entry, params):
        try:
            if entry.format != Field.INTEGER or entry.encoding == Field.BIG_ENDIAN:
                result = Data.from_int_big_endian(value, length)
                return result
            else:
                return Data.from_int_little_endian(value, length)
        except IntegerTooLongError:
            # The value didn't fit in this length... try the next
            # one.
            pass
    else:
        raise VariableIntegerTooLongError(entry, value)

def convert_value(entry, value, length, params=None):
    """Convert a value to the correct type given the entry.

    For example, given an integer field, the string '43' would be converted to
    the number 43."""
    temp_original = value
    if entry.format == Field.BINARY:
        try:
            if isinstance(value, Data):
                value = value.copy()
            elif isinstance(value, int) or isinstance(value, long):
                try:
                    value = Data.from_int_big_endian(value, int(length))
                except UndecodedReferenceError:
                    value = _encode_unknown_variable_length_integer(entry, value, params)
            else:
                value = Data.from_binary_text(_convert_type(entry, value, str))
        except DataError, ex:
            raise FieldDataError(entry, ex)
    elif entry.format == Field.HEX:
        value = Data.from_hex(_convert_type(entry, value, str))
    elif entry.format == Field.TEXT:
        value = _convert_type(entry, value, str)
    elif entry.format == Field.INTEGER:
        value = _convert_type(entry, value, int)
    elif entry.format == Field.FLOAT:
        value = _convert_type(entry, value, float)
    else:
        raise NotImplementedError(entry, value, context)
    return value

class _ContextLength:
    def __init__(self, length, context):
        self.length = length
        self.context = context

    def __int__(self):
        return self.length.evaluate(self.context)

def convert_value_context(entry, value, context, params=None):
    return convert_value(entry, value, _ContextLength(entry.length, context), params)

def _encode_data(entry, value, length):
    if entry.format in (Field.BINARY, Field.HEX):
        assert isinstance(value, Data)
        result = value.copy()
    elif entry.format == Field.TEXT:
        assert isinstance(value, basestring)
        try:
            result = Data(value.encode(entry.encoding))
        except UnicodeDecodeError:
            raise BadEncodingError(entry, value)
    elif entry.format == Field.INTEGER:
        assert isinstance(value, (int, long))
        if length is None:
            raise FieldDataError(entry, 'Unable to encode integer field '
                    'without explicit length')
        assert entry.encoding in [Field.BIG_ENDIAN, Field.LITTLE_ENDIAN]
        if entry.encoding == Field.BIG_ENDIAN:
            result = Data.from_int_big_endian(value, length)
        else:
            result = Data.from_int_little_endian(value, length)
    elif entry.format == Field.FLOAT:
        assert entry.encoding in [Field.BIG_ENDIAN, Field.LITTLE_ENDIAN]
        assert isinstance(value, float)
        if entry.encoding == Field.BIG_ENDIAN:
            result = Data.from_float_big_endian(value, length)
        else:
            result = Data.from_float_little_endian(value, length)
    else:
        raise Exception("Unknown field format of '%s'!" % entry.format)

    return result

def encode_value(entry, value, length=None):
    """
    Convert an object to a dt.Data object.

    Can raise dt.DataError errors.
    """
    if length is None:
        try:
            length = entry.length.evaluate({})
        except UndecodedReferenceError:
            # The length isn't available...
            pass

    try:
        return _encode_data(entry, value, length)
    except DataError, ex:
        raise FieldDataError(entry, ex)


class FieldEncoder(EntryEncoder):
    def _get_default(self, context):
        # We handle strings as a prompt to use the expected value. This is
        # because the named item may be in the output, but not necessarily
        # the value (eg: in the xml representation, it is clearer to not
        # display the expected value).
        for constraint in self.entry.constraints:
            if isinstance(constraint, Equals):
                value = constraint.limit.evaluate(context)
                if isinstance(value, Data):
                    value = self.entry.decode_value(value)
                if isinstance(value, Data):
                    # This is a binary or hex object. For variable length
                    # fields, it may be that the length of the expected value
                    # doesn't match the required length; in this case we have
                    # to add leading nulls.
                    try:
                        length = self.entry.length.evaluate(context)
                    except UndecodedReferenceError, ex:
                        # We don't know what length it should be. Just make
                        # it a multiple of whole bytes.
                        length = len(value)
                        if length % 8:
                            length = length + (8 - length % 8)
                    value = Data('\x00' * (length / 8 + 1), 0, length - len(value)) + value
                break
        else:
            if self.is_hidden:
                range = EntryValueType(self.entry).range(self._params)
                value = Ranges([range]).get_default()
                try:
                    length = self.entry.length.evaluate(context)
                except UndecodedReferenceError, ex:
                    # We don't know, and can't calculate, the length
                    if value == 0:
                        length = 0
                    else:
                        length = 8
                if self.entry.format in [Field.HEX, Field.BINARY]:
                    value = Data.from_int_big_endian(value, length)
                elif self.entry.format in [Field.TEXT]:
                    value = '\x00' * (length / 8 - 1) + chr(value)
            else:
                # We don't have a default for this entry
                raise MissingFieldException(self.entry)
        return value

    def _get_data(self, value, context):
        try:
            length = self.entry.length.evaluate(context)
            return encode_value(self.entry, value, length)
        except UndecodedReferenceError, ex:
            # We don't know how long this entry should be.
            if self.entry.format == self.entry.INTEGER:
                return _encode_unknown_variable_length_integer(self.entry, value, self._params)
            else:
                # All other types (eg: string, binary, hex) have an implicit
                # length that the encoder can use.
                return encode_value(self.entry, value, None)

    def _fixup_value(self, value, context):
        original = value
        if value in [None, '']:
            try:
                value = self._get_default(context)
            except MissingFieldException:
                # If we have an empty string for a variable length field (that
                # may be zero length), it's not missing.
                if value is None or self.entry.format not in [Field.HEX, Field.BINARY, Field.TEXT]:
                    raise
        return convert_value_context(self.entry, value, context, self._params)

    def _encode(self, query, value, context):
        yield self._get_data(value, context)

