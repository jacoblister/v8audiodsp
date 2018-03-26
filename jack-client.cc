// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>

#include "include/libplatform/libplatform.h"
#include "include/v8.h"

class V8Process {
  public:
    v8::Isolate::CreateParams create_params;
    v8::Isolate* isolate;
    v8::Eternal<v8::Context> context;
    v8::Eternal<v8::Function> start_function;
    v8::Eternal<v8::Function> process_function;
    v8::Eternal<v8::Function> dummy_load_function;

    int samples_per_frame;
  public:
    V8Process() {}
    void compile(std::string filename);
    void compile_source(std::string filename);

    void init(int sampling_rate, int samples_per_frame);
    void prerun();
    float *process(float *samples);
    void shutdown(void);
};

void V8Process::compile(std::string source) {
    v8::Local<v8::String> source_func = v8::String::NewFromUtf8(isolate, source.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
    v8::Local<v8::Script> script_func = v8::Script::Compile(this->context.Get(this->isolate), source_func).ToLocalChecked();
    script_func->Run(this->context.Get(this->isolate)).ToLocalChecked();
}

void V8Process::compile_source(std::string filename) {
    std::ifstream t(filename.c_str());
    std::string source((std::istreambuf_iterator<char>(t)),
                    std::istreambuf_iterator<char>());
    v8::Local<v8::String> source_func = v8::String::NewFromUtf8(this->isolate, source.c_str(), v8::NewStringType::kNormal).ToLocalChecked();
    v8::Local<v8::Script> script_func = v8::Script::Compile(this->context.Get(this->isolate), source_func).ToLocalChecked();
    script_func->Run(this->context.Get(this->isolate)).ToLocalChecked();
}

// Extracts a C string from a V8 Utf8Value.
const char* ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

// The callback that is invoked by v8 whenever the JavaScript 'console_log'
// function is called.  Prints its arguments on stdout separated by
// spaces and ending with a newline.
void ConsoleLog(const v8::FunctionCallbackInfo<v8::Value>& args) {
  bool first = true;
  for (int i = 0; i < args.Length(); i++) {
    v8::HandleScope handle_scope(args.GetIsolate());
    if (first) {
      first = false;
    } else {
      printf(" ");
    }
    v8::String::Utf8Value str(args.GetIsolate(), args[i]);
    const char* cstr = ToCString(str);
    printf("%s", cstr);
  }
  printf("\n");
  fflush(stdout);
}

void V8Process::prerun() {
    // Dummy run process function to attempt to optimise
    float *dummybuf = (float *)calloc(samples_per_frame, sizeof(float));
    for(int i = 0; i < 100; i++) {
        this->process(dummybuf);
    }
    free(dummybuf);
}

void V8Process::init(int sampling_rate, int samples_per_frame) {
    this->samples_per_frame = samples_per_frame;

    v8::V8::InitializeICUDefaultLocation("");
    v8::V8::InitializeExternalStartupData("");
    v8::Platform *platform = v8::platform::CreateDefaultPlatform();
    v8::V8::InitializePlatform(platform);
    v8::V8::Initialize();

    // Create a new Isolate and make it the current one.
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator =
      v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    this->isolate = v8::Isolate::New(create_params);
    v8::Isolate::Scope isolate_scope(this->isolate);

    // Create a stack-allocated handle scope.
    v8::HandleScope handle_scope(this->isolate);

    // Bind the global 'console_log' function to the C++ Print callback.
    v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
    global->Set(
      v8::String::NewFromUtf8(isolate, "console_log", v8::NewStringType::kNormal)
          .ToLocalChecked(),
      v8::FunctionTemplate::New(isolate, ConsoleLog));

    // Create a new context.
    v8::Local<v8::Context> local_context = v8::Context::New(this->isolate, NULL, global);
    this->context = v8::Eternal<v8::Context>(this->isolate, local_context);

    // Enter the context for compiling and running script.
    v8::Context::Scope context_scope(local_context);

    // Set console log function
    this->compile("console.log = function (message) { console_log(message) };");

    this->compile_source("process.js");

    v8::Local<v8::Value> start_function_value = local_context->Global()->Get(v8::String::NewFromUtf8(isolate, "start"));
    v8::Local<v8::Function> start_function = v8::Local<v8::Function>::Cast(start_function_value);
    v8::Local<v8::Value> args[] = { v8::Number::New(this->isolate, (double)sampling_rate) };
    start_function->Call(local_context->Global(), 1, args);
    this->start_function = v8::Eternal<v8::Function>(this->isolate, start_function);

    v8::Local<v8::Value> process_function_value = local_context->Global()->Get(v8::String::NewFromUtf8(isolate, "process"));
    v8::Local<v8::Function> process_function = v8::Local<v8::Function>::Cast(process_function_value);
    this->process_function = v8::Eternal<v8::Function>(this->isolate, process_function);

    v8::Local<v8::Value> dummy_load_function_value = local_context->Global()->Get(v8::String::NewFromUtf8(isolate, "dummyLoad"));
    v8::Local<v8::Function> dummy_load_function = v8::Local<v8::Function>::Cast(dummy_load_function_value);
    this->dummy_load_function = v8::Eternal<v8::Function>(this->isolate, dummy_load_function);
}

float *V8Process::process(float *samples) {
    v8::Locker locker(this->isolate);
    v8::HandleScope handle_scope(this->isolate);
    v8::Local<v8::Context> local_context = this->context.Get(this->isolate);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::ArrayBuffer> arrayBuffer = v8::ArrayBuffer::New(this->isolate, samples, this->samples_per_frame * sizeof(float));
    v8::Local<v8::Float32Array> float32Array = v8::Float32Array::New(arrayBuffer, 0, this->samples_per_frame);

    v8::Handle<v8::Value> args[1];
    args[0] = float32Array;
    this->process_function.Get(this->isolate)->Call(local_context->Global(), 1, args);

    return samples;
}

void V8Process::shutdown(void) {
    // Dispose the isolate and tear down V8.
    this->isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete this->create_params.array_buffer_allocator;
}

V8Process v8process;
volatile bool backgroundRunning = 0;

jack_client_t *jack_client;
jack_port_t *jack_input_port;
jack_port_t *jack_output_port;

#include <unistd.h>

int process_jack(jack_nframes_t nframes, void *arg) {
//    static bool is_init = 0;
//    if (!is_init) {
//        printf("init jack thread\n");
//        v8process.init(jack_get_sample_rate(jack_client), nframes);
//        is_init = 1;
//    }

	jack_default_audio_sample_t *in, *out;

	in = (jack_default_audio_sample_t *)jack_port_get_buffer(jack_input_port, nframes);
	out = (jack_default_audio_sample_t *)jack_port_get_buffer(jack_output_port, nframes);

    if (!backgroundRunning) {
        v8process.process(in);
    }

	memcpy(out, in, sizeof (jack_default_audio_sample_t) * nframes);

	return 0;
}

void call_dummy_load() {
    v8::Locker locker(v8process.isolate);
    v8::HandleScope handle_scope(v8process.isolate);
    v8::Local<v8::Context> local_context = v8process.context.Get(v8process.isolate);
    v8::Context::Scope context_scope(local_context);

    v8::Local<v8::Value> args[] = { v8::Number::New(v8process.isolate, 48000) };
    backgroundRunning = 1;
    v8process.dummy_load_function.Get(v8process.isolate)->Call(local_context->Global(), 1, args);
    backgroundRunning = 0;
}

void run_jack(V8Process &v8process) {
	const char **ports;
	const char *client_name = "v8process";
	const char *server_name = NULL;
	jack_options_t options = JackNullOption;
	jack_status_t status;

	/* open a client connection to the JACK server */
    jack_client = jack_client_open(client_name, options, &status, server_name);
    v8process.init(jack_get_sample_rate(jack_client), jack_get_buffer_size(jack_client));
    v8process.prerun();

	jack_input_port = jack_port_register (jack_client, "input",
		                     			  JACK_DEFAULT_AUDIO_TYPE,
					                      JackPortIsInput, 0);
	jack_output_port = jack_port_register (jack_client, "output",
					                       JACK_DEFAULT_AUDIO_TYPE,
					                       JackPortIsOutput, 0);
    jack_set_process_callback(jack_client, process_jack, 0);
	jack_activate(jack_client);

    while(1) {
        usleep(1000000);

//        call_dummy_load();
    }
}

int main(int argc, char* argv[]) {
    run_jack(v8process);
    v8process.shutdown();

    return 0;
}
