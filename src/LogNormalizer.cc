
#include "LogNormalizer.h"
#include <Event.h>

extern "C" {
#include <json.h>
}

using namespace plugin::Bro_Lognorm;

static OpaqueType* lognormalizer_type = new OpaqueType("lognormalizer");

LogNormalizer::LogNormalizer(EventHandlerPtr evt_unparsed) : evt_unparsed(evt_unparsed)
	{
	ctx = ln_initCtx();
	}

LogNormalizer::~LogNormalizer()
	{
	ln_exitCtx(ctx);
	}

bool LogNormalizer::LoadRuleFile(const char* filename)
	{
	return ln_loadSamples(ctx, filename) == 0;
	}

bool LogNormalizer::LoadRuleFromString(const char* str)
	{
	return ln_loadSamplesFromString(ctx, str) == 0;
	}

bool LogNormalizer::Normalize(const char* line)
	{
	json_object* json = NULL;

	if ( ln_normalize(ctx, line, strlen(line), &json) != 0 )
		return false;

	// Raise an event for unparsed lines
	if ( json_object_object_get_ex(json, "unparsed-data", NULL) )
		{
		if ( evt_unparsed )
			{
			val_list* args = new val_list;
			args->append(new StringVal(line));
			mgr.QueueEvent(evt_unparsed, args);
			}
		return false;
		}

	// Retrieve tags and parameters
	json_object* tags = NULL;
	FieldList fields;
	
	json_object_iterator it = json_object_iter_begin(json);
	json_object_iterator it_end = json_object_iter_end(json);
	while ( !json_object_iter_equal(&it, &it_end) )
		{
		const char* key = json_object_iter_peek_name(&it);
		json_object* val = json_object_iter_peek_value(&it);

		if ( strcmp(key, "event.tags") == 0 )
			tags = val;
		else
			fields[key] = ParseField(val);

		json_object_iter_next(&it);
		}

	// Generate events for each tag
	int tags_len = json_object_array_length(tags);
	for ( int i = 0; i < tags_len; i++ )
		{
		json_object* tag = json_object_array_get_idx(tags, i);
		const char* evt_name = json_object_get_string(tag);

		EventHandlerPtr evt = event_registry->Lookup(evt_name);
		if ( ! evt )
			{
			reporter->Warning("No handler found for event triggered by lognorm: %s", evt_name);
			continue;
			}

		// Create a separate parameter list for each event
		mgr.QueueEvent(evt, BuildArgs(evt, fields));
		}

	// Consume initial reference
	for ( auto &fld : fields )
		Unref(fld.second);

	json_object_put(json);
	return true;
	}

Val* LogNormalizer::ParseField(json_object* field)
	{
	Val* field_val = NULL;
	int field_type = json_object_get_type(field);

	switch ( field_type ) {
	case json_type_boolean:
		field_val = new Val(json_object_get_boolean(field), TYPE_BOOL);
		break;
	case json_type_int:
		field_val = new Val(json_object_get_int64(field), TYPE_INT);
		break;
	case json_type_double:
		field_val = new Val(json_object_get_double(field), TYPE_DOUBLE);
		break;
	case json_type_string:
		field_val = new StringVal(json_object_get_string(field));
		break;
	default:
		field_val = new StringVal("Unsupported type: " + std::to_string(field_type));
	}

	return field_val;
	}

val_list* LogNormalizer::BuildArgs(EventHandlerPtr evt, const FieldList &fields)
	{
	val_list* args = new val_list;
	RecordType* evt_args = evt->FType()->Args();

	for ( int i = 0; i < evt_args->NumFields(); i++ )
		{
		FieldList::const_iterator fld = fields.find(evt_args->FieldName(i));
		if ( fld != fields.end() )
			{
			if ( same_type(fld->second->Type(), evt_args->FieldType(i)) )
				{
				args->append(fld->second->Ref());
				continue;
				}
			else
				{
				reporter->Error("Incompatible argument types for event and "
					"liblognorm rule. Expected %s(%s: %s)",
					evt->Name(), evt_args->FieldName(i),
					type_name(fld->second->Type()->Tag()));
				}
			}
		else
			{
			reporter->Warning("Argument not defined by lognorm rule: %s(%s: %s)",
				evt->Name(), evt_args->FieldName(i),
				type_name(evt_args->FieldType(i)->Tag()));
			}
		//TODO: Create value of requested type?
		args->append(new Val());
		}

	return args;
	}

LogNormalizerVal::LogNormalizerVal(LogNormalizer* ln) : OpaqueVal(lognormalizer_type)
	{
	normalizer = ln;
	}

LogNormalizerVal::~LogNormalizerVal()
	{
	delete normalizer;
	}

LogNormalizer* LogNormalizerVal::GetNormalizer() const
	{
	return normalizer;
	}
