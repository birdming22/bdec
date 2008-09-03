## vim:set syntax=mako:
<%! 
  from bdec.choice import Choice
  from bdec.data import Data
  from bdec.field import Field
  from bdec.sequence import Sequence
  from bdec.sequenceof import SequenceOf
 %>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "${entry.name |filename}.h"
%for e in iter_optional_common(entry):
#include "${e.name |filename}.h"
%endfor
#include "variable_integer.h"

<%def name="update_length_reference(entry)">
  %if is_length_referenced(entry):
    *${entry.name + ' length' |variable} = ${'initial length' |variable} - buffer->num_bits;
  %endif
</%def>

<%def name="compare_binary_expected(entry, expected)">
  %if len(entry.expected) < 32:
    if (get_integer(&actual) != ${int(entry.expected)})
  %else:
    BitBuffer expected = {(unsigned char*)${settings.c_string(expected.bytes())}, 0, ${len(entry.expected)}};
    int isMatch = 1;
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

<%def name="decodeField(entry)">
    if (${settings.value(entry.length)} > buffer->num_bits)
    {
        return 0;
    }
    ${settings.ctype(entry)} value;
  %if entry.min is not None or entry.max is not None:
    BitBuffer field_data = *buffer;
    field_data.num_bits = ${settings.value(entry.length)};
  %endif
  %if entry.format == Field.INTEGER:
    %if entry.encoding == Field.BIG_ENDIAN:
    value = decode_integer(buffer, ${settings.value(entry.length)});
    %else:
    value = decode_little_endian_integer(buffer, ${settings.value(entry.length)});
    %endif
    %if is_value_referenced(entry):
    *${entry.name |variable} = value;
    %endif
  %elif entry.format == Field.TEXT:
    int i;
    int ${entry.name + ' buffer length' |variable} = ${settings.value(entry.length)} / 8;
    value = malloc(${entry.name + ' buffer length' |variable} + 1);
    value[${entry.name + ' buffer length' |variable}] = 0;
    for (i = 0; i < ${entry.name + ' buffer length' |variable}; ++i)
    {
        value[i] = decode_integer(buffer, 8);
    }
  %elif entry.format == Field.HEX:
    int i;
    assert((${settings.value(entry.length)}) % 8 == 0);
    value.length = ${settings.value(entry.length)} / 8;
    value.buffer = malloc(value.length);
    for (i = 0; i < value.length; ++i)
    {
        value.buffer[i] = decode_integer(buffer, 8);
    }
  %elif entry.format == Field.BINARY:
    value.start_bit = buffer->start_bit;
    value.num_bits = ${settings.value(entry.length)};
    // 0 bits = 0 bytes, 1-8 bits = 1 byte, 9-16 bytes = 2 bytes...
    int numBytes = (buffer->start_bit + buffer->num_bits + 7) / 8;
    value.buffer = malloc(numBytes);
    memcpy(value.buffer, buffer->buffer, numBytes);
    buffer->start_bit += value.num_bits;
    buffer->buffer += buffer->start_bit / 8;
    buffer->start_bit %= 8;
    buffer->num_bits -= value.num_bits;
  %else:
    #error Unknown field type ${entry}
  %endif
     %if entry.expected is not None:
       %if entry.format == entry.INTEGER:
    if (value != ${int(entry.expected)})
       %elif entry.format == entry.BINARY:
           <% 
           extra_bits = 8 - len(entry.expected) % 8
           expected = entry.expected + Data('\x00', start=0, end=extra_bits)
           %>
    BitBuffer actual = value;
    ${compare_binary_expected(entry, expected)}
       %elif entry.format == entry.HEX:
    BitBuffer actual = {value.buffer, 0, value.length * 8};
    ${compare_binary_expected(entry, entry.expected)}
       %elif entry.format == entry.TEXT:
    if (memcmp(value, ${settings.c_string(entry.expected.bytes())}, ${len(entry.expected) / 8}) != 0)
       %else:
#error Field of type ${entry.format} not currently supported for an expected value!
       %endif
    {
      %if entry.format != Field.INTEGER:
        ${settings.free_name(entry)}(&value);
      %endif
        return 0;
    }
     %endif
    %if entry.min is not None:
    if (get_integer(&field_data) < ${settings.value(entry.min)})
    {
      %if entry.format != Field.INTEGER:
        ${settings.free_name(entry)}(&value);
      %endif
        return 0;
    }
    %endif
    %if entry.max is not None:
    if (get_integer(&field_data) > ${settings.value(entry.max)})
    {
      %if entry.format != Field.INTEGER:
        ${settings.free_name(entry)}(&value);
      %endif
        return 0;
    }
    %endif
    %if not entry.is_hidden():
    (*result) = value;
    %else:
      %if entry.format == Field.TEXT:
    ${settings.free_name(entry)}(&value);
      %elif entry.format == Field.BINARY:
    ${settings.free_name(entry)}(&value);
      %elif entry.format == Field.HEX:
    ${settings.free_name(entry)}(&value);
      %endif
    %endif

    %if is_end_sequenceof(entry):
    *${'should end' |variable} = 1;
    %endif
    ${update_length_reference(entry)}
    return 1;
</%def>

<%def name="decodeSequence(entry)">
    %for i, child in enumerate(entry.children):
    if (!${settings.decode_name(child)}(buffer${settings.params(entry, i, '&result->%s' % variable(esc_name(i, entry.children)))}))
    {
        %for j, previous in enumerate(entry.children[:i]):
            %if contains_data(previous):
        ${settings.free_name(previous)}(&result->${settings.var_name(j, entry.children)});
            %endif
        %endfor
        return 0;
    }
    %endfor
    %if entry.value is not None:
    int value = ${settings.value(entry.value)};
      %if contains_data(entry):
    result->value = value;
      %endif
      %if is_value_referenced(entry):
    *${entry.name |variable} = value;
      %endif
    %endif

    %if is_end_sequenceof(entry):
    *${'should end' |variable} = 1;
    %endif
    ${update_length_reference(entry)}
    return 1;
</%def>

<%def name="decodeSequenceOf(entry)">
    int i;
    %if entry.count is not None:
    int num_items;
    num_items = ${settings.value(entry.count)};
      %if contains_data(entry):
    result->count = num_items;
    result->items = malloc(sizeof(${settings.ctype(entry.children[0])}) * result->count);
      %endif
    for (i = 0; i < num_items; ++i)
    {
    %else:
      %if contains_data(entry):
    result->items = 0;
    result->count = 0;
      %endif
      %if entry.length is not None:
    while (buffer->num_bits > 0)
      %else:
    while (!${'should end' |variable})
      %endif
    {
      %if contains_data(entry):
        i = result->count;
        ++result->count;
        result->items = realloc(result->items, sizeof(${settings.ctype(entry.children[0])}) * (result->count + 1));
      %endif
    %endif
        if (!${settings.decode_name(entry.children[0])}(buffer${settings.params(entry, 0, '&result->items[i]')}))
        {
      %if contains_data(entry):
            int j;
            for (j=0; j<i; ++j)
            {
                ${settings.free_name(entry.children[0])}(&result->items[j]);
            }
            free(result->items);
      %endif
            return 0;
        }
    }

    %if is_end_sequenceof(entry):
    *${'should end' |variable} = 1;
    %endif
    ${update_length_reference(entry)}
    return 1;
</%def>

<%def name="decodeChoice(entry)">
    %if contains_data(entry):
    memset(result, 0, sizeof(${settings.ctype(entry)}));
    %endif
    BitBuffer temp;
    %for i, child in enumerate(entry.children):
    temp = *buffer;
    <% temp_name = variable('temp ' + esc_name(i, entry.children)) %>
    ${settings.ctype(child)}* ${temp_name} = malloc(sizeof(${settings.ctype(child)}));
    if (${settings.decode_name(child)}(&temp${params(entry, i, temp_name)}))
    {
        *buffer = temp;
      %if contains_data(child):
        result->${settings.var_name(i, entry.children)} = ${temp_name};
      %else:
        free(${temp_name});
      %endif
        ${update_length_reference(entry)}
        return 1;
    }
    free(${temp_name});
    %endfor
    // Decode failed, no options succeeded...
    return 0;
</%def>

## Recursively create functions for decoding the entries contained within this protocol specification.
<%def name="recursiveDecode(entry, is_static=True)">
%for child in entry.children:
  %if child not in common:
${recursiveDecode(child)}
  %endif
%endfor

<% static = "static " if is_static else "" %>
%if contains_data(entry) or (isinstance(entry, Field) and entry.format != Field.INTEGER):
${static}void ${settings.free_name(entry)}(${settings.ctype(entry)}* value)
{
  %if isinstance(entry, Field):
    %if entry.format == Field.TEXT:
    free(*value);
    %elif entry.format == Field.HEX:
    free(value->buffer);
    %elif entry.format == Field.BINARY:
    free(value->buffer);
    %endif
  %elif isinstance(entry, Sequence):
    %for i, child in enumerate(entry.children):
        %if contains_data(child):
    ${settings.free_name(child)}(&value->${settings.var_name(i, entry.children)});
        %endif
    %endfor
  %elif isinstance(entry, SequenceOf):
    int i;
    for (i = 0; i < value->count; ++i)
    {
        ${settings.free_name(entry.children[0])}(&value->items[i]);
    }
    free(value->items);
  %elif isinstance(entry, Choice):
    %for i, child in enumerate(entry.children):
      %if contains_data(child):
    if (value->${settings.var_name(i, entry.children)} != 0)
    {
        ${settings.free_name(child)}(value->${settings.var_name(i, entry.children)});
        free(value->${settings.var_name(i, entry.children)});
    }
      %endif
    %endfor
  %else:
    <% raise Exception("Don't know how to free objects of type '%s'" % entry) %>
  %endif
}
%endif


${static}int ${settings.decode_name(entry)}(BitBuffer* buffer${settings.define_params(entry)})
{
  %for local in local_vars(entry):
      int ${local} = 0;
  %endfor
  %if is_length_referenced(entry):
      int ${'initial length' |variable} = buffer->num_bits;
  %endif
  %if isinstance(entry, Field):
    ${decodeField(entry)}
  %elif isinstance(entry, Sequence):
    ${decodeSequence(entry)}
  %elif isinstance(entry, SequenceOf):
    ${decodeSequenceOf(entry)}
  %elif isinstance(entry, Choice):
    ${decodeChoice(entry)}
  %endif
}
</%def>

${recursiveDecode(entry, False)}

<%def name="printText(text, ws_offset)">
  %if ws_offset == 0:
    ## We have to print in two runs, as we cannot print zero space characters 
    ## in a single print statement.
    if (offset + ${ws_offset} > 0)
    {
        printf("%*c", offset + ${ws_offset}, ' ');
    }
    printf("${text}");
  %else:
    printf("%*c${text}", offset + ${ws_offset}, ' ');
  %endif
</%def>

## Recursively create the function for printing the entries.
##
## We buffer the output, as the generated output will be filtered to adjust
## the whitespace offset.
<%def name="recursivePrint(item, varname, ws_offset, iter_postfix)" buffered="True">
  %if item in common and item is not entry:
    %if contains_data(item):
    ${settings.print_name(item)}(&${varname}, offset + ${ws_offset});
    %endif
  %elif contains_data(item):
    %if isinstance(item, Field):
      %if not item.is_hidden():
    ${printText("<%s>" % xmlname(item.name), ws_offset)}
        %if item.format == Field.INTEGER:
    printf("%i", ${varname}); 
        %elif item.format == Field.TEXT:
    printf("%s", ${varname});
        %elif item.format == Field.HEX:
        <% iter_name = variable(item.name + ' counter' + str(iter_postfix.next())) %>
    int ${iter_name};
    for (${iter_name} = 0; ${iter_name} < ${varname}.length; ++${iter_name})
    {
        printf("%x", ${varname}.buffer[${iter_name}]);
    }
        %elif item.format == Field.BINARY:
        <% copy_name = variable('copy of ' + item.name + str(iter_postfix.next())) %>
        <% iter_name = variable(item.name + ' counter' + str(iter_postfix.next())) %>
    BitBuffer ${copy_name} = ${varname};
    int ${iter_name};
    for (${iter_name} = 0; ${iter_name} < ${varname}.num_bits; ++${iter_name})
    {
        printf("%i", decode_integer(&${copy_name}, 1));
    }
        %else:
    #error Don't know how to print ${item}
        %endif
      %endif
    %else:
    ## Print everything other than fields
      %if not item.is_hidden():
    ${printText("<%s>\\n" % xmlname(item.name), ws_offset)}
      %endif
      <% next_offset = (ws_offset + 4) if not item.is_hidden() else ws_offset %>
      %if isinstance(item, Sequence):
        %for i, child in enumerate(item.children):
${recursivePrint(child, '%s.%s' % (varname, variable(esc_name(i, item.children))), next_offset, iter_postfix)}
        %endfor
        %if item.value is not None and not item.is_hidden():
    printf("%*%i\n", offset + ${ws_offset+4}, ${varname}.value); 
        %endif
      %elif isinstance(item, SequenceOf):
        <% iter_name = variable(item.name + ' counter' + str(iter_postfix.next())) %>
    int ${iter_name};
    for (${iter_name} = 0; ${iter_name} < ${varname}.count; ++${iter_name})
    {
${recursivePrint(item.children[0], '%s.items[%s]' % (varname, iter_name), next_offset, iter_postfix) | ws(4)}
    }
      %elif isinstance(item, Choice):
        %for i, child in enumerate(item.children):
          %if contains_data(child):
    if (${'%s.%s' % (varname, variable(esc_name(i, item.children)))} != 0)
    {
${recursivePrint(child, "(*%s.%s)" % (varname, variable(esc_name(i, item.children))), next_offset, iter_postfix) | ws(4)}
    }
          %endif
        %endfor
      %else:
    #error Don't know how to print ${item}
      %endif
    %endif
    %if not item.is_hidden():
      %if not isinstance(item, Field):
    ${printText("</%s>\\n" % xmlname(item.name), ws_offset)}
      %else:
    printf("</${item.name |xmlname}>\n");
      %endif
    %endif
  %endif
</%def>

void ${settings.print_name(entry)}(${settings.ctype(entry)}* data, int offset)
{
${recursivePrint(entry, '(*data)', 0, iter(xrange(100)))}
}

