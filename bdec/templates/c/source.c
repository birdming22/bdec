## vim:set syntax=mako:

<%! 
  from bdec.choice import Choice
  from bdec.constraints import Equals
  from bdec.data import Data
  from bdec.encode.field import encode_value
  from bdec.encode.choice import get_default_option_params
  from bdec.expression import Constant, ValueResult, UndecodedReferenceError
  from bdec.field import Field
  from bdec.inspect.solver import solve_expression
  from bdec.inspect.type import expression_range as erange
  from bdec.sequence import Sequence
  from bdec.sequenceof import SequenceOf
 %>

/*  Copyright (C) 2008-2010 Henry Ludemann
    This file is part of the bdec decoder library.

    The bdec decoder library is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    The bdec decoder library is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, see
    <http://www.gnu.org/licenses/>.
  
 This file incorporates work covered by the following copyright and  
 permission notice:  
  
    Copyright (c) 2010, PRESENSE Technologies GmbH
    All rights reserved.
    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions of source code must retain the above copyright
          notice, this list of conditions and the following disclaimer.
        * Redistributions in binary form must reproduce the above copyright
          notice, this list of conditions and the following disclaimer in the
          documentation and/or other materials provided with the distribution.
        * Neither the name of the PRESENSE Technologies GmbH nor the
          names of its contributors may be used to endorse or promote products
          derived from this software without specific prior written permission.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL PRESENSE Technologies GmbH BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#define __STDC_FORMAT_MACROS

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "${entry.name |filename}.h"
%for e in iter_optional_common(entry):
#include "${e.name |filename}.h"
%endfor
#include "variable_integer.h"

<%def name="compare_binary_expected(value, entry, expected, local_vars)">
  %if expression_range(expected).max is not None and expression_range(expected).max < 2^64:
    if (get_integer(&${value}) != ${settings.value(entry, expected)})
  %else:
    ## NOTE: We are assuming that if a constraint is longer than a long int, it
    ## is a constant constraint.
    <% assert isinstance(expected, Constant) %>
    <% padding_len = (8 - len(expected.value) % 8) if len(expected.value) % 8 <  8 else 0 %>
    <% data = expected.value + Data('\x00', 0, padding_len) %>
    <% local_vars.append(('BitBuffer', 'expected')) %>
    expected.buffer = (unsigned char*)${settings.c_string(data.bytes())};
    expected.start_bit = 0;
    expected.num_bits = ${len(expected.value)};
    <% local_vars.append(('int', 'isMatch')) %>
    isMatch = 1;
    <% local_vars.append(('BitBuffer', 'actual')) %>
    actual = ${value};
    while (expected.num_bits > 0)
    {
        if (decode_integer(&expected, 1) != decode_integer(&actual, 1))
        {
            isMatch = 0;
            break;
        }
    }
    if (!isMatch)
  %endif
</%def>

<%def name="checkConstraints(entry, value, result, local_vars)">
  %for constraint in entry.constraints:
    %if isinstance(constraint, Equals):
      %if settings.is_numeric(settings.ctype(entry)) or isinstance(entry, Sequence):
    if (${value} != ${settings.value(entry, constraint.limit)})
      %elif settings.ctype(entry) == 'BitBuffer':
    ${compare_binary_expected(value, entry, constraint.limit, local_vars)}
      %elif settings.ctype(entry) in ['Buffer', 'Text']:
      <% assert isinstance(constraint.limit, Constant) %>
      <% expected = encode_value(entry, constraint.limit.value) %>
    if (${value}.length != ${len(expected) / 8} ||
            memcmp(${value}.buffer, ${settings.c_string(expected.bytes())}, ${len(expected) / 8}) != 0)
      %else:
      <%raise Exception("Don't know how to compare '%s' types for equality in entry '%s'!" % (constraint, entry)) %>
      %endif
    %else:
      ## For all constraints other than equality we treat the value as an integer...
      %if settings.is_numeric(settings.ctype(entry)):
    if (${value} ${constraint.type} ${settings.value(entry, constraint.limit)})
      %elif settings.ctype(entry) == 'BitBuffer':
    if (${get_integer(entry)}(&${value}) ${constraint.type} ${str(constraint.limit)})
      %elif settings.ctype(entry) in ['Buffer', 'Text']:
    if (${value}.length != 1 || (unsigned int)${value}.buffer[0] ${constraint.type} ${str(constraint.limit)})
      %else:
      <%raise Exception("Don't know how to compare '%s' types!" % constraint) %>
      %endif
    %endif
    {
      %if result is not None and should_free(entry):
        ${settings.free_name(entry)}(${result});
      %endif
        return 0;
    }
  %endfor
</%def>

<%def name="decodeField(entry, local_vars)">
    <%
       value = variable(entry.name + ' value')
       if is_value_used(entry):
           local_vars.append((settings.ctype(entry), value))
    %>
  %if settings.is_numeric(settings.ctype(entry)):
      <% prefix = 'little_endian_' if entry.encoding == Field.LITTLE_ENDIAN else '' %>
      <% prefix = 'long_%s' % prefix if EntryValueType(entry).range(raw_params).max > 0xffffffff else prefix %>
    <% assignment = '%s = ' % value if is_value_used(entry) else '' %>
    ${assignment}decode_${prefix}integer(buffer, ${settings.value(entry, entry.length)});
    %if is_value_referenced(entry):
    *${entry.name |variable} = ${value};
    %endif
  %elif entry.format == Field.TEXT:
    <% local_vars.append(('unsigned int', 'i')) %>
    ${value}.length = ${settings.value(entry, entry.length)} / 8;
    ${value}.buffer = (char*)malloc(${value}.length + 1);
    if (${value}.buffer == 0)
    {
        return 0;
    }
    ${value}.buffer[${value}.length] = 0;
    for (i = 0; i < ${value}.length; ++i)
    {
        ${value}.buffer[i] = decode_integer(buffer, 8);
    }
  %elif entry.format == Field.HEX:
    <% local_vars.append(('unsigned int', 'i')) %>
    assert((${settings.value(entry, entry.length)}) % 8 == 0);
    ${value}.length = ${settings.value(entry, entry.length)} / 8;
    ${value}.buffer = (unsigned char*)malloc(${value}.length);
    if (${value}.buffer == 0)
    {
        return 0;
    }
    for (i = 0; i < ${value}.length; ++i)
    {
        ${value}.buffer[i] = decode_integer(buffer, 8);
    }
  %elif entry.format == Field.BINARY:
    ${value}.start_bit = buffer->start_bit;
    ${value}.num_bits = ${settings.value(entry, entry.length)};
    <% local_vars.append(('unsigned int', 'numBytes')) %>
    numBytes = (buffer->start_bit + buffer->num_bits + 7) / 8;
    ${value}.buffer = (unsigned char*)malloc(numBytes);
    if (${value}.buffer == 0)
    {
        return 0;
    }
    memcpy(${value}.buffer, buffer->buffer, numBytes);
    buffer->start_bit += ${value}.num_bits;
    buffer->buffer += buffer->start_bit / 8;
    buffer->start_bit %= 8;
    buffer->num_bits -= ${value}.num_bits;

    %if is_value_referenced(entry):
    *${entry.name |variable} = ${get_integer(entry)}(&${value});
    %endif
  %elif entry.format == Field.FLOAT:
    <% encoding = 'BDEC_LITTLE_ENDIAN' if entry.encoding == Field.LITTLE_ENDIAN \
         else 'BDEC_BIG_ENDIAN' %>

    switch (${settings.value(entry, entry.length)})
    {
    case 32:
        ${value} = decodeFloat(buffer, ${encoding});
        break;
    case 64:
        ${value} = decodeDouble(buffer, ${encoding});
        break;
    default:
        return 0;
    }
  %else:
    <% raise Exception('Unknown field type %s' % entry) %>
  %endif
    ${checkConstraints(entry, value, '&%s' % value, local_vars)}
    %if contains_data(entry):
    (*result) = ${value};
    %else:
      %if should_free(entry):
    ${settings.free_name(entry)}(&${value});
      %endif
    %endif
</%def>

<%def name="decodeSequence(entry, local_vars)">
    %for i, child in enumerate(entry.children):
    if (!${settings.decode_name(child.entry)}(buffer${settings.decode_passed_params(entry, i, '&result->%s' % settings.var_name(entry, i))}))
    {
        %for j, previous in enumerate(entry.children[:i]):
            %if child_contains_data(previous):
        ${settings.free_name(previous.entry)}(&result->${settings.var_name(entry, j)});
            %endif
        %endfor
        return 0;
    }
    %endfor
    %if entry.value is not None:
    <% value = variable(entry.name + ' value') %>
    <% local_vars.append((ctype(EntryValueType(entry)), value)) %>
    ${value} = ${settings.value(entry, entry.value)};
    ${checkConstraints(entry, value, 'result', local_vars)}
      %if contains_data(entry):
        %if settings.is_numeric(settings.ctype(entry)):
    *result = ${value};
        %else:
    result->value = ${value};
        %endif
      %endif
      %if is_value_referenced(entry):
    *${entry.name |variable} = ${value};
      %endif
    %endif
</%def>

<%def name="decodeSequenceOf(entry, local_vars)">
    <% child_type = settings.ctype(entry.children[0].entry) %>
    %if entry.end_entries:
    ${'should end'|variable} = 0;
    %endif

    %if entry.count is not None:
    ## There is a 'count' avaiable; use that to allocate the buffer up front.
    <% local_vars.append((settings.sequenceof_count_ctype(entry), 'i')) %>
    <% local_vars.append((settings.sequenceof_count_ctype(entry), 'num_items')) %>
    num_items = ${settings.value(entry, entry.count)};
      %if contains_data(entry):
    result->count = num_items;
        %if child_contains_data(entry.children[0]):
    result->items = (${child_type}*)malloc(sizeof(${child_type}) * result->count);
    if (result->items == 0)
    {
        return 0;
    }
        %endif
      %endif
    for (i = 0; i < num_items; ++i)
    {
    %else:
      ## There isn't a count, so keep decoding until we run out of data.
      %if contains_data(entry):
    <% local_vars.append((settings.sequenceof_count_ctype(entry), 'i')) %>
        %if child_contains_data(entry.children[0]):
    result->items = 0;
        %endif
    result->count = 0;
      %endif
      %if entry.end_entries:
    while (!${'should end' |variable})
      %else:
    while (buffer->num_bits > 0)
      %endif
    {
      %if contains_data(entry):
        i = result->count;
        ++result->count;
        %if child_contains_data(entry.children[0]):
        result->items = (${child_type}*)realloc(result->items, sizeof(${child_type}) * (result->count));
        %endif
      %endif
    %endif

      ## Check for the 'should end' happening early
      %if entry.end_entries:
        <% validate_end = '%s || ' % variable('should end') %>
    %else:
        <% validate_end = '' %>
      %endif

        if (${validate_end}!${settings.decode_name(entry.children[0].entry)}(buffer${settings.decode_passed_params(entry, 0, '&result->items[i]')}))
        {
      %if child_contains_data(entry.children[0]):
            <% local_vars.append((settings.sequenceof_count_ctype(entry), 'j')) %>
            for (j=0; j< i; ++j)
            {
                ${settings.free_name(entry.children[0].entry)}(&result->items[j]);
            }
            free(result->items);
      %endif
            return 0;
        }
    }

      %if entry.end_entries:
    if (!${'should end'|variable})
    {
        /* The data finished without receiving an 'end sequence'! */
        %if contains_data(entry):
        ${settings.free_name(entry)}(result);
        %endif
        return 0;
    }
      %endif

</%def>

<%def name="populateChoiceOutputs(entry, i, params)">
  %for name, temp in settings.option_output_temporaries(entry, i, params).items():
    %if name not in [local.name for local in params.get_locals(entry)]:
    *${name} = ${temp};
    %else:
    ${name} = ${temp};
    %endif
  %endfor
</%def>

<%def name="decodeChoice(entry, local_vars)">
    %if contains_data(entry):
    memset(result, 0, sizeof(${settings.ctype(entry)}));
    %endif
    <% local_vars.append(('BitBuffer', 'temp')) %>
    <% names = esc_names(('temp %s' % c.name for c in entry.children), variable) %>
    %for i, child in enumerate(entry.children):
      %if child_contains_data(child) and (is_recursive(entry, child.entry) or not contains_data(entry)):
        <% local_vars.append((settings.ctype(child.entry), names[i])) %>
      %endif
    %endfor
    %for i, child in enumerate(entry.children):
      %if is_recursive(entry, child.entry) or not contains_data(entry):
    <% temp_name = names[i] %>
      %else:
    <% temp_name = 'result->value.' + settings.var_name(entry, i) %>
      %endif
    <% if_ = "if" if i == 0 else 'else if' %>
    ${if_} (temp = *buffer, ${settings.decode_name(child.entry)}(&temp${settings.decode_passed_params(entry, i, "&%s" % temp_name)}))
    {
      ${populateChoiceOutputs(entry, i, decode_params)}
      %if contains_data(entry):
          %if settings.children_contain_data(entry):
        result->option = ${settings.enum_value(entry, i)};
          %else:
        *result = ${settings.enum_value(entry, i)};
          %endif
      %endif
        *buffer = temp;
      %if child_contains_data(child) and is_recursive(entry, child.entry):
        <% child_type = settings.ctype(child.entry) %>
        result->value.${settings.var_name(entry, i)} = (${child_type}*)malloc(sizeof(${child_type}));
    if (result->value.${settings.var_name(entry, i)} == 0)
    {
        return 0;
    }
        *result->value.${settings.var_name(entry, i)} = ${temp_name};
      %endif
    }
    %endfor
    else
    {
        /* Decode failed, no options succeeded... */
        return 0;
    }
</%def>

## Recursively create functions for decoding the entries contained within this protocol specification.
<%def name="recursiveDecode(entry, is_static=True)">
%for child in entry.children:
  %if child.entry not in common:
${recursiveDecode(child.entry)}
  %endif
%endfor

<% static = "static " if is_static else "" %>
%if should_free(entry):
${static}void ${settings.free_name(entry)}(${settings.ctype(entry)}* value)
{
  %if isinstance(entry, Field):
    %if settings.is_numeric(settings.ctype(entry)):

    %elif entry.format == Field.TEXT:
    free(value->buffer);
    %elif entry.format == Field.HEX:
    free(value->buffer);
    %elif entry.format == Field.BINARY:
    free(value->buffer);
    %endif
  %elif isinstance(entry, Sequence):
    %for i, child in enumerate(entry.children):
        %if child_contains_data(child):
    ${settings.free_name(child.entry)}(&value->${settings.var_name(entry, i)});
        %endif
    %endfor
  %elif isinstance(entry, SequenceOf):
    %if child_contains_data(entry.children[0]):
    ${settings.sequenceof_count_ctype(entry)} i;
    for (i = 0; i < value->count; ++i)
    {
        ${settings.free_name(entry.children[0].entry)}(&value->items[i]);
    }
    free(value->items);
    %endif
  %elif isinstance(entry, Choice):
      %if settings.children_contain_data(entry):
    switch(value->option)
      %else:
    switch(*value)
      %endif
    {
    %for i, child in enumerate(entry.children):
    case ${enum_value(entry, i)}:
      %if child_contains_data(child):
        <% child_var = "value->value.%s" % settings.var_name(entry, i) %>
        %if not is_recursive(entry, child.entry):
          <% child_var = '&' + child_var %>
        %endif
        ${settings.free_name(child.entry)}(${child_var});
        %if is_recursive(entry, child.entry):
        free(${child_var});
        %endif
      %endif
        break;
    %endfor
    }
  %else:
    <% raise Exception("Don't know how to free objects of type '%s'" % entry) %>
  %endif
}
%endif


${static}int ${settings.decode_name(entry)}(BitBuffer* buffer${settings.define_params(entry)})
{
  %for local in settings.local_variables(entry):
    ${settings.ctype(local.type)} ${local.name};
  %endfor
  %if is_length_referenced(entry):
    unsigned int ${'initial length' |variable} = buffer->num_bits;
  %endif
  %if entry.length is not None:
    <% length = variable(entry.name + " expected length") %>
    unsigned int ${length} = ${settings.value(entry, entry.length)};
    unsigned int ${'unused number of bits' |variable} = buffer->num_bits - ${length};
  %endif
  <%
     lookup = {Field: decodeField, Sequence: decodeSequence,
         SequenceOf: decodeSequenceOf, Choice: decodeChoice}
     local_vars = []
     decode_source_code = capture(lookup[entry.__class__], entry, local_vars) %>
  %for type, name in local_vars:
    ${type} ${name};
  %endfor
  %if entry.length is not None:
    if (${length} > buffer->num_bits)
    {
        /* Not enough data */
        return 0;
    }
    buffer->num_bits = ${length};
  %endif
  ${decode_source_code}
  %if is_end_sequenceof(entry):
    *${'should end' |variable} = 1;
  %endif
  %if entry.length is not None:
    if (buffer->num_bits != 0)
    {
        /* The entry didn't use all of the data */
      %if contains_data(entry):
        ${settings.free_name(entry)}(result);
      %endif
        return 0;
    }
    buffer->num_bits = ${'unused number of bits' |variable};
  %endif
  %if is_length_referenced(entry):
    *${entry.name + ' length' |variable} = ${'initial length' |variable} - buffer->num_bits;
  %endif
    return 1;
}
</%def>

${recursiveDecode(entry, False)}

<%def name="print_whitespace()">
    if (offset > 0)
    {
        printf("%*c", offset, ' ');
    }
</%def>


## Recursively create the function for printing the entries.
##
## We buffer the output, as the generated output will be filtered to adjust
## the whitespace offset.
<%def name="print_child(child, name, offset=2)">
   %if not is_hidden(child.name):
     %if contains_data(child.entry):
${settings.print_name(child.entry)}(${name}, offset + ${offset}, ${'"%s"' % xmlname(child.name)});
     %else:
${settings.print_name(child.entry)}(offset + ${offset}, ${'"%s"' % xmlname(child.name)});
     %endif
   %endif
</%def>

<%def name="printField(entry, local_vars)">
    ${print_whitespace()}
      %if not contains_data(entry):
    printf(${'"<%s />\\n"'}, name);
      %elif entry.format == Field.INTEGER:
    printf(${'"<%s>' + settings.printf_format(settings.ctype(entry)) + '</%s>\\n"'}, name, *data, name);
      %elif entry.format == Field.TEXT:
    printf(${'"<%s>"'}, name);
    print_escaped_string(data);
    printf(${'"</%s>\\n"'}, name);
      %elif entry.format == Field.HEX:
    printf(${'"<%s>"'}, name);
        <% iter_name = variable(entry.name + ' counter') %>
        <% local_vars.append(('unsigned int', iter_name)) %>
    for (${iter_name} = 0; ${iter_name} < data->length; ++${iter_name})
    {
        printf("%02x", data->buffer[${iter_name}]);
    }
    printf(${'"</%s>\\n"'}, name);
      %elif entry.format == Field.BINARY:
    printf(${'"<%s>"'}, name);
        <% copy_name = variable('copy of ' + entry.name) %>
        <% iter_name = variable(entry.name + ' whitespace counter') %>
        <% local_vars.append(('BitBuffer', copy_name)) %>
        %if settings.is_numeric(settings.ctype(entry)):
          <% length = EntryLengthType(entry).range(raw_params).min %>
    ${copy_name}.buffer = data;
    ${copy_name}.start_bit = 8 - ${length};
    ${copy_name}.num_bits = ${length};
        %else:
    ${copy_name} = *data;
        %endif
        <% local_vars.append(('unsigned int', iter_name)) %>
    ${iter_name} = ${copy_name}.num_bits > 8 ? ${copy_name}.num_bits % 8 : 8;
    for (; ${copy_name}.num_bits != 0; --${iter_name})
    {
        if (${iter_name} == 0)
        {
            putchar(' ');
            ${iter_name} = 8;
        }
        printf("%u", decode_integer(&${copy_name}, 1));
    }
    printf(${'"</%s>\\n"'}, name);
      %elif entry.format == Field.FLOAT:
    printf(${'"<%s>%f</%s>\\n"'}, name, *data, name);
      %else:
    <% raise Exception("Don't know how to print %s" % entry) %>
      %endif
</%def>

<%def name="printChoice(entry, local_vars)">
      %if settings.children_contain_data(entry):
    switch(data->option)
      %else:
    switch(*data)
      %endif
    {
      %for i, child in enumerate(entry.children):
    case ${enum_value(entry, i)}:
        %if not is_hidden(child.name):
          <% child_var = "data->value.%s" % (settings.var_name(entry, i)) %>
          %if not is_recursive(entry, child.entry):
            <% child_var = '&%s' % child_var %>
          %endif
        ${print_child(child, child_var, 0)|ws(8)}
        %endif
        break;
      %endfor
    }
</%def>

<%def name="printSequence(entry, local_vars)">
  %if settings.is_numeric(settings.ctype(entry)):
    ${print_whitespace()}
      %if contains_data(entry):
    printf(${'"<%s>' + settings.printf_format(settings.ctype(entry)) + '</%s>\\n"'}, name, *data, name);
      %else:
    printf(${'"<%s />\\n"'}, name);
      %endif
  %else:
    ${print_whitespace()}
    printf(${'"<%s>\\n"'}, name);
      %for i, child in enumerate(entry.children):
        %if not is_hidden(child.name):
    ${print_child(child, "&data->%s" % settings.var_name(entry, i))|ws(4)}
        %endif
      %endfor
      %if entry.value is not None and not entry.is_hidden():
    offset += 2;
    ${print_whitespace()}
    <% format = settings.printf_format(settings.ctype(EntryValueType(entry))) %>
    printf("${format}\n", data->value);
    offset -= 2;
      %endif
    ${print_whitespace()}
    printf(${'"</%s>\\n"'}, name);
  %endif
</%def>

<%def name="printSequenceOf(entry, local_vars)">
    ${print_whitespace()}
    printf(${'"<%s>\\n"'}, name);
      %if not is_hidden(entry.children[0].name):
        <% iter_name = variable(entry.name + ' counter') %>
        <% local_vars.append((settings.sequenceof_count_ctype(entry), iter_name)) %>
    for (${iter_name} = 0; ${iter_name} < data->count; ++${iter_name})
    {
        ${print_child(entry.children[0], '&data->items[%s]' % (iter_name))|ws(8)}
    }
      %endif
    ${print_whitespace()}
    printf(${'"</%s>\\n"'}, name);
</%def>

<%def name="recursivePrint(entry, is_static)">
%for child in entry.children:
  %if not is_hidden(entry.name) and not is_hidden(child.name) and  child.entry not in common:
${recursivePrint(child.entry, True)}
  %endif
%endfor

<% static = "static " if is_static else "" %>
%if contains_data(entry):
${static}void ${settings.print_name(entry)}(${settings.ctype(entry)}* data, unsigned int offset, const char* name)
%else:
${static}void ${settings.print_name(entry)}(unsigned int offset, const char* name)
%endif
{
  <%
      lookup = {Field: printField, Sequence: printSequence,
          Choice: printChoice, SequenceOf: printSequenceOf}
      local_vars = []
      print_source_code = capture(lookup[entry.__class__], entry, local_vars) if not is_hidden(entry.name) else ''
      %>
  %for type, name in local_vars:
    ${type} ${name};
  %endfor
  ${print_source_code}
}
</%def>

${recursivePrint(entry, False)}

## ========= Entry encoding ===========

<%def name="ifChildEncode(entry, i, child_variable, local_vars, buffer_name='result', is_successful=True)">
  <% child = entry.children[i] %>
  <% prefix = '' if is_successful else '!' %>
  %if contains_data(child.entry):
      %if not child_contains_data(child):
        ## The child is a common entry that has been hidden. We need to create
        ## a mock object, as the encode function expects an object to encode.
        ## We need to populate this variable with any output parameters that
        ## have been referenced from it.
        <% child_variable = '%s' % variable('unused %s' % child.name) %>
    memset(&${child_variable}, 0, sizeof(${settings.ctype(child.entry)}));
        <% raw_params = raw_encode_expression_params.get_passed_variables(entry, child) %>
        <% esc_params = stupid_ugly_expression_encode_params.get_passed_variables(entry, child) %>
        %for raw_param, esc_param in zip(raw_params, esc_params):
            %if raw_param.direction == raw_param.OUT and raw_encode_expression_params.is_output_param_used(entry, child, raw_param):
              ## For every expression parameter that is passed out, that is an
              ## value that during encoding comes from the value parameter we
              ## pass in. Thus we have to set the mock parameters appropriately...
              <% value_name = settings.get_child_variable(entry, i, raw_param, child_variable) %>
    ${value_name} = ${esc_param.name};
              <%
              # If the parameter we just solved is a sequence that references
              # other visible entries in the mocked sequence, we need to
              # recursively solve for those entries too.
              dependencies = [
                (settings.get_reference_stack([(child.entry, child.name)], raw_param.name.split('.')[1:]), value_name, raw_param.name)
                ]
              %>
              %while dependencies:
                <% entry_stack, value_name, name = dependencies.pop() %>
                <% referenced = entry_stack[-1][0] %>
                %if isinstance(referenced, Sequence):
                  <% def mock_name(ref_entry, ref, params):
                        result = settings.relative_reference_name(entry_stack, ref, child_variable)
                        if ':' not in ref.name:
                            # The entry we are solving references another entry; we
                            # have to check to see if we should solve that one too.
                            dependency = (settings.get_reference_stack(entry_stack, ref.name.split('.')), result[1], ref.name)
                            if dependency not in dependencies:
                                dependencies.append(dependency)
                        return result
                        %>
    ${solve(referenced, referenced.value, value_name, variable('mock %s' % name), local_vars, mock_name)}
                %endif
              %endwhile
            %endif
        %endfor
        <% child_variable = '&%s' % child_variable %>
      %endif
  %else:
    <% child_variable = "don't look here, move along." %>
  %endif
    if (${prefix}${settings.encode_name(child.entry)}(${buffer_name}${encode_passed_params(entry, i, child_variable)}))
</%def>

<%def name="encodeField(entry, local_vars)">
    <% should_free_buffer = False %>
    %if contains_data(entry):
    <%
       # This entry has a visible value; use its passed in value object.
       if settings.is_numeric(ctype(entry)):
           value_name = 'value'
       else:
           value_name = '*value'
       source_name = value_name
    %>
    %else:
        <% expected = settings.get_equals(entry) %>
        <% value_name = variable(entry.name + ' value') %>
        <% source_name = value_name %>
        %if expected is not None:
            ## This any has an expected value; use it when encoding.
            <% local_vars.append((settings.ctype(entry), value_name)) %>
            %if settings.is_numeric(settings.ctype(entry)):
    ${value_name} = ${settings.value(entry, expected)};
            %elif entry.format == fld.Field.TEXT:
    ${value_name}.buffer = (char*)${settings.c_string(expected.value)};
    ${value_name}.length = ${len(expected.value)};
            %elif entry.format == fld.Field.BINARY:
                <%
                    # This is a bitbuffer type; add leading null bytes so we can
                    # represent it in bytes.
                    null = Data('\x00', 0, 8 - (len(expected.value) % 8))
                    data = null + expected.value
                    %>
    ${value_name}.buffer = (unsigned char*)${c_string(data.bytes())};
    ${value_name}.start_bit = ${len(null)};
    ${value_name}.num_bits = ${len(data) - len(null)};
            %elif entry.format == fld.Field.HEX:
    ${value_name}.buffer = (char*)${settings.c_string(expected.value.bytes())};
    ${value_name}.length = ${len(expected.value) / 8};
            %else:
                <% raise Exception("Don't know how to define a constant for %s!" % entry) %>
            %endif
        %elif is_value_referenced(entry):
            ## This entry is used in an expression. Use the value that is passed
            ## when encoding.
            <% name = variable(entry.name) %>
            <% source_name = name %>
            %if settings.is_numeric(settings.ctype(entry)):
                <% local_vars.append((settings.ctype(entry), value_name)) %>
    ${value_name} = ${name};
            %else:
                <% long_name = 'long_' if EntryValueType(entry).range(raw_params).max > 0xffffffff else '' %>
                <% endian = 'big' if entry.encoding == Field.BIG_ENDIAN else 'little' %>
                <% length = settings.value(entry, entry.length) %>
                <% local_vars.append(('char', 'tempBuffer[(%s + 7) / 8]' % length)) %>
                <% local_vars.append(('struct EncodedData', 'binaryValue')) %>
    binaryValue.buffer = tempBuffer;
    binaryValue.num_bits = 0;
    binaryValue.allocated_length_bytes = (${length} + 7) / 8;
    encode_${long_name}${endian}_endian_integer(${name}, ${length}, &binaryValue);
    <% local_vars.append(('BitBuffer', value_name)) %>
    ${value_name}.buffer= (unsigned char*)binaryValue.buffer;
    ${value_name}.start_bit = 0;
    ${value_name}.num_bits = ${length};
            %endif
        %else:
            ## This entry doesn't have a known value; mock it.
            <%
            local_vars.append((settings.ctype(entry), value_name))
            try:
                # If this entry has a fixed (known) length, just allocate a null
                # buffer on the stack.
                length = entry.length.evaluate({})
                data = '"\\000"' * ((length + 7) / 8)
            except UndecodedReferenceError:
                if _is_length_known(entry):
                    # There is an explicit length for this entry.
                    length = value(entry, entry.length, encode_params)
                    data = "calloc((%s) / 8, 1)" % length
                    should_free_buffer = True
                else:
                    # The length isn't known; just default it to zero.
                    length = 0
                    data = '""' %>
            %if settings.is_numeric(settings.ctype(entry)):
    ${value_name} = 0;
            %elif entry.format == fld.Field.TEXT:
    ${value_name}.buffer = (char*)${data};
    ${value_name}.length = (${length}) / 8;
            %elif entry.format == fld.Field.HEX:
    ${value_name}.buffer = (unsigned char*)${data};
    ${value_name}.length = (${length}) / 8;
            %elif entry.format == fld.Field.BINARY:
    ${value_name}.buffer = (unsigned char*)${data};
    ${value_name}.start_bit = 0;
    ${value_name}.num_bits = ${length};
            %else:
                <% raise Exception("Don't know how to define a constant for %s!" % entry) %>
            %endif
        %endif
    %endif
    ${checkConstraints(entry, source_name, None, local_vars)}
    %if entry.format == Field.INTEGER:
      <% long_name = 'long_' if EntryValueType(entry).range(raw_params).max > 0xffffffff else '' %>
      <% length = settings.value(entry, entry.length) %>
      <% endian = 'big' if entry.encoding == Field.BIG_ENDIAN else 'little' %>
    encode_${long_name}${endian}_endian_integer(${value_name}, ${length}, result);
    %elif entry.format == Field.BINARY:
      %if settings.is_numeric(settings.ctype(entry)):
        <% length = EntryLengthType(entry).range(raw_params).min %>
        <% local_vars.append(('BitBuffer', 'copy')) %>
    copy.buffer = &${value_name};
    copy.start_bit = 8 - ${length};
    copy.num_bits = ${length};
      %else:
        <% local_vars.append(('BitBuffer', 'copy')) %>
    copy = ${value_name};
      %endif
    appendBitBuffer(result, &copy);
    %elif entry.format == Field.TEXT:
    appendText(result, &${value_name});
    %elif entry.format == Field.HEX:
    appendBuffer(result, &${value_name});
    %elif entry.format == Field.FLOAT:
      <% floatType = 'Float' if (EntryLengthType(entry).range(raw_params).max == 32) else 'Double' %>
      <% encoding = 'BIG_ENDIAN' if entry.encoding == entry.BIG_ENDIAN else 'LITTLE_ENDIAN' %>
    append${floatType}(${value_name}, BDEC_${encoding}, result);
    %else:
      <% raise Exception("Don't know how to encode field %s!" % entry) %>
    %endif

    %if should_free_buffer:
    free(${value_name}.buffer);
    %endif
    %if encode_params.is_value_referenced(entry):
    *${entry.name |variable} = ${value_name};
    %endif
</%def>

<%def name="solve(entry, expression, value_name, prefix, local_vars, solved_name=None)">
    <% solved_name = solved_name or settings.local_reference_name %>
    <% constant, components = settings.breakup_expression(expression, entry) %>
    <% remainder = variable('%s remainder' % prefix) %>
    <% local_vars.append((settings.type_from_range(erange(expression, entry, raw_decode_params)), remainder)) %>
    ${remainder} = ${value_name};
    %if constant != 0:
    ${remainder} -= ${settings.value(entry, constant, encode_params)};
    %endif
    %for ref, expr, invert_expr in components:
    <% is_temp, solve_name = solved_name(entry, ref, encode_params) %>
    <%
        if is_temp:
            local_vars.append((settings.type_from_range(erange(expr, entry, raw_decode_params)), solve_name))
    %>
    ${solve_name} = ${settings.value(entry, invert_expr, encode_params, expression, remainder)};
    ${remainder} -= ${settings.value(entry, expr, encode_params, ref_name=solved_name)};
    %endfor
    if (${remainder} != 0)
    {
        /* We failed to solve this expression... */
        return 0;
    }
</%def>

<%def name="encodeSequence(entry, local_vars)">
    %if entry.value is not None:
      <% value_name, should_solve = settings.get_sequence_value(entry) %>
      %if should_solve:
    ${checkConstraints(entry, value_name, None, local_vars)}
    ${solve(entry, entry.value, value_name, 'value', local_vars)}
      %endif
    %endif

    <% temp_buffers = [] %>
    %for i, start_temp_buffer, buffer_name, end_temp_buffers in settings.sequence_encoder_order(entry):
      <% child = entry.children[i] %>
      %if start_temp_buffer is not None:
        <% local_vars.append(('struct EncodedData', start_temp_buffer)) %>
    memset(&${start_temp_buffer}, 0, sizeof(struct EncodedData));
        <% temp_buffers.append(start_temp_buffer) %>
      %endif
    ${ifChildEncode(entry, i, '&value->%s' % settings.var_name(entry, i), local_vars, buffer_name, is_successful=False)}
    {
      %for temp_buffer in temp_buffers:
        free(${temp_buffer}.buffer);
      %endfor
        return 0;
    }
      %for temp_buffer in end_temp_buffers:
    appendEncodedBuffer(result, &${temp_buffer});
      %endfor
    %endfor
    %for temp_buffer in temp_buffers:
    free(${temp_buffer}.buffer);
    %endfor
    %if variable(entry.name) in [p.name for p in encode_params.get_params(entry) if p.direction == p.OUT]:
    *${entry.name |variable} = ${value_name};
    %endif
</%def>

<%def name="populateUnusedOptionOutputs(choice, i)">
  ## Now populate any outputs that weren't explicitly set by this option.
  <% names = dict(zip((p.name for p in raw_encode_params.get_params(choice)),
             (p.name for p in  encode_params.get_params(choice)))) %>
  %for name, value in get_default_option_params(choice, choice.children[i], raw_encode_expression_params).items():
      *${names[name]} = ${value};
  %endfor
</%def>

<%def name="encodeChoice(entry, local_vars)">
  %if contains_data(entry):
    <% option = 'value->option' if settings.children_contain_data(entry) else '*value' %>
    switch (${option})
    {
      %for i, child in enumerate(entry.children):
    case ${enum_value(entry, i)}:
          <%
              name = "value->value.%s" % settings.var_name(entry, i)
              if not is_recursive(entry, child.entry):
                  # Recursive types are stored as pointers
                  name = '&' + name
          %>
      ${ifChildEncode(entry, i, name, local_vars, is_successful=False)}
      {
          return 0;
      }
      ${populateChoiceOutputs(entry, i, encode_params)}
      ${populateUnusedOptionOutputs(entry, i)}
      break;
      %endfor
    default:
      return 0;
    }
  %else:
    ## This choice doesn't contain data; try each option in turn until one
    ## works.
    <% local_vars.append(('int', 'numActualBits')) %>
    numActualBits = result->num_bits;
      %for i, child in enumerate(entry.children):
    ${ifChildEncode(entry, i, '', local_vars)}
    {
        ${populateChoiceOutputs(entry, i, encode_params)}
        ${populateUnusedOptionOutputs(entry, i)}
        goto encode_successful;
    }
    result->num_bits = numActualBits;
      %endfor
    return 0;
encode_successful:
  %endif
</%def>

<%def name="encodeSequenceof(entry, local_vars)">
    %if contains_data(entry) or entry.count is not None:
    <% local_vars.append((settings.sequenceof_count_ctype(entry), 'i')) %>
    i = 0;
    %endif
    %if contains_data(entry):
    for (i = 0; i < value->count; ++i)
    {
        ${ifChildEncode(entry, 0, '&value->items[i]', local_vars, is_successful=False)}
        {
            return 0;
        }
    }
    %endif

    %if entry.count is not None:
    ${solve(entry, entry.count, 'i', 'count', local_vars)}
    %endif
</%def>

<%def name="recursiveEncode(entry, is_static)">
%for child in entry.children:
  %if child.entry not in common and not is_empty_sequenceof(entry):
${recursiveEncode(child.entry, True)}
  %endif
%endfor

<% static = "static " if is_static else "" %>
${static}int ${settings.encode_name(entry)}(struct EncodedData* result${settings.encode_params(entry)})
{
  %for local in settings.encode_local_variables(entry):
    ${settings.ctype(local.type)} ${local.name};
  %endfor

  %if entry.length is not None or is_length_referenced(entry):
  int startBit = result->num_bits;
  %endif

  <%
     lookup = {Field: encodeField, Sequence: encodeSequence,
         Choice: encodeChoice, SequenceOf: encodeSequenceof}
     local_vars = []
     encode_source_code = capture(lookup[entry.__class__], entry, local_vars)
     if entry.length is not None:
        encode_source_code += capture(solve, entry, entry.length, 'result->num_bits - startBit', 'length', local_vars)
     endif
   %>
  %for type, name in local_vars:
    ${type} ${name};
  %endfor
  ${encode_source_code}

  %if is_length_referenced(entry):
    *${entry.name + ' length' |variable} = result->num_bits - startBit;
  %endif
    return 1;
}

</%def>

%if generate_encoder:
${recursiveEncode(entry, False)}
%endif
