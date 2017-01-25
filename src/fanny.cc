#include "fanny.h"
#include <nan.h>
#include "fann-includes.h"
#include <iostream>
#include "utils.h"
#include "training-data.h"

namespace fanny {

class RunWorker : public Nan::AsyncWorker {

public:
	RunWorker(Nan::Callback *callback, std::vector<fann_type> & _inputs, v8::Local<v8::Object> fannyHolder) : Nan::AsyncWorker(callback), inputs(_inputs) {
		SaveToPersistent("fannyHolder", fannyHolder);
		fanny = Nan::ObjectWrap::Unwrap<FANNY>(fannyHolder);
	}
	~RunWorker() {}

	void Execute() {
		fann_type *fannOutputs = fanny->fann->run(&inputs[0]);
		if (fanny->fann->get_errno()) {
			SetErrorMessage(fanny->fann->get_errstr().c_str());
			fanny->fann->reset_errno();
			fanny->fann->reset_errstr();
			return;
		}
		unsigned int numOutputs = fanny->fann->get_num_output();
		for (unsigned int idx = 0; idx < numOutputs; idx++) {
			outputs.push_back(fannOutputs[idx]);
		}
	}

	void HandleOKCallback() {
		Nan::HandleScope scope;
		v8::Local<v8::Value> args[] = {
			Nan::Null(),
			fannDataToV8Array(&outputs[0], outputs.size())
		};
		callback->Call(2, args);
	}

	std::vector<fann_type> inputs;
	std::vector<fann_type> outputs;
	FANNY *fanny;
};

class LoadFileWorker : public Nan::AsyncWorker {
public:
	LoadFileWorker(Nan::Callback *callback, std::string _filename) : Nan::AsyncWorker(callback), filename(_filename) {}
	~LoadFileWorker() {}

	void Execute() {
		struct fann *ann = fann_create_from_file(filename.c_str());
		if (!ann) return SetErrorMessage("Error loading FANN file");
		fann = new FANN::neural_net(ann);
		fann_destroy(ann);
	}

	void HandleOKCallback() {
		Nan::HandleScope scope;
		if (fann->get_errno()) {
			v8::Local<v8::Value> args[] = { Nan::Error(std::string(fann->get_errstr()).c_str()) };
			delete fann;
			callback->Call(1, args);
		} else {
			v8::Local<v8::Value> externFann = Nan::New<v8::External>(fann);
			v8::Local<v8::Function> ctor = Nan::New(FANNY::constructorFunction);
			v8::Local<v8::Value> ctorArgs[] = { externFann };
			v8::Local<v8::Value> cbargs[] = {
				Nan::Null(),
				Nan::NewInstance(ctor, 1, ctorArgs).ToLocalChecked()
			};
			callback->Call(2, cbargs);
		}
	}

	std::string filename;
	FANN::neural_net *fann;
};

class SaveFileWorker : public Nan::AsyncWorker {
public:
	SaveFileWorker(Nan::Callback *callback, v8::Local<v8::Object> fannyHolder, std::string _filename, bool _isFixed) :
		Nan::AsyncWorker(callback), filename(_filename), isFixed(_isFixed)
	{
		SaveToPersistent("fannyHolder", fannyHolder);
		fanny = Nan::ObjectWrap::Unwrap<FANNY>(fannyHolder);
	}
	~SaveFileWorker() {}

	void Execute() {
		bool hasError = false;
		if (isFixed) {
			decimalPoint = fanny->fann->save_to_fixed(filename);
		} else {
			hasError = !fanny->fann->save(filename);
			decimalPoint = 0;
		}
		if (fanny->fann->get_errno()) {
			SetErrorMessage(fanny->fann->get_errstr().c_str());
			fanny->fann->reset_errno();
			fanny->fann->reset_errstr();
		} else if (hasError) {
			SetErrorMessage("Error saving FANN file");
		}
	}

	void HandleOKCallback() {
		Nan::HandleScope scope;
		v8::Local<v8::Value> args[] = { Nan::Null(), Nan::New(decimalPoint) };
		callback->Call(2, args);
	}

	FANNY *fanny;
	std::string filename;
	bool isFixed;
	int decimalPoint;
};

class TrainWorker : public Nan::AsyncWorker {
public:
	FANNY *fanny;
	TrainingData *trainingData;
	bool trainFromFile;
	std::string filename;
	bool isCascade;
	unsigned int maxIterations;
	unsigned int iterationsBetweenReports;
	float desiredError;
	bool singleEpoch;
	bool isTest;

	float retVal;

	TrainWorker(
		Nan::Callback *callback,
		v8::Local<v8::Object> fannyHolder,
		Nan::MaybeLocal<v8::Object> maybeTrainingDataHolder,
		bool _trainFromFile,
		std::string _filename,
		bool _isCascade,
		unsigned int _maxIterations,
		unsigned int _iterationsBetweenReports,
		float _desiredError,
		bool _singleEpoch,
		bool _isTest
	) : Nan::AsyncWorker(callback), trainFromFile(_trainFromFile), filename(_filename),
	isCascade(_isCascade), maxIterations(_maxIterations), iterationsBetweenReports(_iterationsBetweenReports),
	desiredError(_desiredError), singleEpoch(_singleEpoch), isTest(_isTest), retVal(-1) {
		SaveToPersistent("fannyHolder", fannyHolder);
		fanny = Nan::ObjectWrap::Unwrap<FANNY>(fannyHolder);
		if (!maybeTrainingDataHolder.IsEmpty()) {
			v8::Local<v8::Object> trainingDataHolder = maybeTrainingDataHolder.ToLocalChecked();
			SaveToPersistent("tdHolder", trainingDataHolder);
			trainingData = Nan::ObjectWrap::Unwrap<TrainingData>(trainingDataHolder);
		}
	}
	~TrainWorker() {}

	void Execute() {
		#ifndef FANNY_FIXED
		if (isTest) {
			retVal = fanny->fann->test_data(*trainingData->trainingData);
		} else if (singleEpoch) {
			retVal = fanny->fann->train_epoch(*trainingData->trainingData);
		} else if (!trainFromFile && !isCascade) {
			fanny->fann->train_on_data(*trainingData->trainingData, maxIterations, iterationsBetweenReports, desiredError);
		} else if (trainFromFile && !isCascade) {
			fanny->fann->train_on_file(filename, maxIterations, iterationsBetweenReports, desiredError);
		} else if (!trainFromFile && isCascade) {
			fanny->fann->cascadetrain_on_data(*trainingData->trainingData, maxIterations, iterationsBetweenReports, desiredError);
		} else if (trainFromFile && isCascade) {
			fanny->fann->cascadetrain_on_file(filename, maxIterations, iterationsBetweenReports, desiredError);
		}
		if (fanny->fann->get_errno()) {
			SetErrorMessage(fanny->fann->get_errstr().c_str());
			fanny->fann->reset_errno();
			fanny->fann->reset_errstr();
		} else if (!singleEpoch && !isTest) {
			retVal = fanny->fann->get_MSE();
		}
		#endif
	}

	void HandleOKCallback() {
		Nan::HandleScope scope;
		v8::Local<v8::Value> args[] = { Nan::Null(), Nan::New(retVal) };
		callback->Call(2, args);
	}
};

void FANNY::Init(v8::Local<v8::Object> target) {
	// Create new function template for this JS class constructor
	v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
	// Set the class name
	tpl->SetClassName(Nan::New("FANNY").ToLocalChecked());
	// Set the number of "slots" to allocate for fields on this class, not including prototype methods
	tpl->InstanceTemplate()->SetInternalFieldCount(1);
	// Save a constructor reference
	FANNY::constructorFunctionTpl.Reset(tpl);

	// Add prototype methods
	Nan::SetPrototypeMethod(tpl, "save", save);
	Nan::SetPrototypeMethod(tpl, "saveToFixed", saveToFixed);
	Nan::SetPrototypeMethod(tpl, "setCallback", setCallback);
	Nan::SetPrototypeMethod(tpl, "trainEpoch", trainEpoch);
	Nan::SetPrototypeMethod(tpl, "trainOnData", trainOnData);
	Nan::SetPrototypeMethod(tpl, "trainOnFile", trainOnFile);
	Nan::SetPrototypeMethod(tpl, "cascadetrainOnData", cascadetrainOnData);
	Nan::SetPrototypeMethod(tpl, "cascadetrainOnFile", cascadetrainOnFile);
	Nan::SetPrototypeMethod(tpl, "run", run);
	Nan::SetPrototypeMethod(tpl, "getNumInput", getNumInput);
	Nan::SetPrototypeMethod(tpl, "getNumOutput", getNumOutput);
	Nan::SetPrototypeMethod(tpl, "getTotalNeurons", getTotalNeurons);
	Nan::SetPrototypeMethod(tpl, "getTotalConnections", getTotalConnections);
	Nan::SetPrototypeMethod(tpl, "getNumLayers", getNumLayers);
	Nan::SetPrototypeMethod(tpl, "getBitFail", getBitFail);
	Nan::SetPrototypeMethod(tpl, "getMSE", getMSE);
	Nan::SetPrototypeMethod(tpl, "getLearningRate", getLearningRate);
	Nan::SetPrototypeMethod(tpl, "getQuickPropDecay", getQuickPropDecay);
	Nan::SetPrototypeMethod(tpl, "getQuickPropMu", getQuickPropMu);
	Nan::SetPrototypeMethod(tpl, "getRpropIncreaseFactor", getRpropIncreaseFactor);
	Nan::SetPrototypeMethod(tpl, "getRpropDecreaseFactor", getRpropDecreaseFactor);
	Nan::SetPrototypeMethod(tpl, "getRpropDeltaZero", getRpropDeltaZero);
	Nan::SetPrototypeMethod(tpl, "getRpropDeltaMin", getRpropDeltaMin);
	Nan::SetPrototypeMethod(tpl, "getRpropDeltaMax", getRpropDeltaMax);
	Nan::SetPrototypeMethod(tpl, "runAsync", runAsync);
	Nan::SetPrototypeMethod(tpl, "initWeights", initWeights);
	Nan::SetPrototypeMethod(tpl, "testData", testData);
	Nan::SetPrototypeMethod(tpl, "getLayerArray", getLayerArray);
	Nan::SetPrototypeMethod(tpl, "getBiasArray", getBiasArray);
	Nan::SetPrototypeMethod(tpl, "train", train);
	Nan::SetPrototypeMethod(tpl, "test", test);
	Nan::SetPrototypeMethod(tpl, "scaleTrain", scaleTrain);
	Nan::SetPrototypeMethod(tpl, "setScalingParams", setScalingParams);

	// Create the loadFile function
	v8::Local<v8::FunctionTemplate> loadFileTpl = Nan::New<v8::FunctionTemplate>(loadFile);
	v8::Local<v8::Function> loadFileFunction = Nan::GetFunction(loadFileTpl).ToLocalChecked();

	// Assign a property called 'FANNY' to module.exports, pointing to our constructor
	v8::Local<v8::Function> ctorFunction = Nan::GetFunction(tpl).ToLocalChecked();
	Nan::Set(ctorFunction, Nan::New("loadFile").ToLocalChecked(), loadFileFunction);
	FANNY::constructorFunction.Reset(ctorFunction);
	Nan::Set(target, Nan::New("FANNY").ToLocalChecked(), ctorFunction);
}


Nan::Persistent<v8::FunctionTemplate> FANNY::constructorFunctionTpl;
Nan::Persistent<v8::Function> FANNY::constructorFunction;

FANNY::FANNY(FANN::neural_net *_fann) : fann(_fann) {}

FANNY::~FANNY() {
	delete fann;
	constructorFunctionTpl.Empty();
	constructorFunction.Empty();
}

NAN_METHOD(FANNY::loadFile) {
	if (info.Length() != 2) return Nan::ThrowError("Requires filename and callback");
	std::string filename = *v8::String::Utf8Value(info[0]);
	Nan::Callback * callback = new Nan::Callback(info[1].As<v8::Function>());
	Nan::AsyncQueueWorker(new LoadFileWorker(callback, filename));
}

NAN_METHOD(FANNY::New) {
	// Ensure arguments
	if (info.Length() != 1) {
		return Nan::ThrowError("Requires single argument");
	}

	FANN::neural_net *fann;

	if (Nan::New(FANNY::constructorFunctionTpl)->HasInstance(info[0])) {
		// Copy constructor
		FANNY *other = Nan::ObjectWrap::Unwrap<FANNY>(info[0].As<v8::Object>());
		fann = new FANN::neural_net(*other->fann);
	} else if (info[0]->IsString()) {
		// Load-from-file constructor
		fann = new FANN::neural_net(std::string(*v8::String::Utf8Value(info[0])));
	} else if (info[0]->IsExternal()) {
		// Internal from-instance constructor
		fann = (FANN::neural_net *)info[0].As<v8::External>()->Value();
	} else if (info[0]->IsObject()) {
		// Options constructor

		// Get the options argument
		v8::Local<v8::Object> optionsObj(info[0].As<v8::Object>());

		// Variables for individual options
		std::string optType;
		std::vector<unsigned int> optLayers;
		float optConnectionRate = 0.5;

		// Get the type option
		Nan::MaybeLocal<v8::Value> maybeType = Nan::Get(optionsObj, Nan::New("type").ToLocalChecked());
		if (!maybeType.IsEmpty()) {
			v8::Local<v8::Value> localType = maybeType.ToLocalChecked();
			if (localType->IsString()) {
				optType = std::string(*(v8::String::Utf8Value(localType)));
			}
		}

		// Get the layers option
		Nan::MaybeLocal<v8::Value> maybeLayers = Nan::Get(optionsObj, Nan::New("layers").ToLocalChecked());
		if (!maybeLayers.IsEmpty()) {
			v8::Local<v8::Value> localLayers = maybeLayers.ToLocalChecked();
			if (localLayers->IsArray()) {
				v8::Local<v8::Array> arrayLayers = localLayers.As<v8::Array>();
				uint32_t length = arrayLayers->Length();
				for (uint32_t idx = 0; idx < length; ++idx) {
					Nan::MaybeLocal<v8::Value> maybeIdxValue = Nan::Get(arrayLayers, idx);
					if (!maybeIdxValue.IsEmpty()) {
						v8::Local<v8::Value> localIdxValue = maybeIdxValue.ToLocalChecked();
						if (localIdxValue->IsNumber()) {
							unsigned int idxValue = localIdxValue->Uint32Value();
							optLayers.push_back(idxValue);
						}
					}
				}
			}
		}
		if (optLayers.size() < 2) {
			return Nan::ThrowError("layers option is required with at least 2 layers");
		}

		// Get the connectionRate option
		Nan::MaybeLocal<v8::Value> maybeConnectionRate = Nan::Get(optionsObj, Nan::New("connectionRate").ToLocalChecked());
		if (!maybeConnectionRate.IsEmpty()) {
			v8::Local<v8::Value> localConnectionRate = maybeConnectionRate.ToLocalChecked();
			if (localConnectionRate->IsNumber()) {
				optConnectionRate = localConnectionRate->NumberValue();
			}
		}

		// Construct the neural_net underlying class
		if (!optType.compare("standard") || optType.empty()) {
			fann = new FANN::neural_net(FANN::network_type_enum::LAYER, (unsigned int)optLayers.size(), (const unsigned int *)&optLayers[0]);
		} else if(optType.compare("sparse")) {
			fann = new FANN::neural_net(optConnectionRate, optLayers.size(), &optLayers[0]);
		} else if (optType.compare("shortcut")) {
			fann = new FANN::neural_net(FANN::network_type_enum::SHORTCUT, optLayers.size(), &optLayers[0]);
		} else {
			return Nan::ThrowError("Invalid type option");
		}
	} else {
		return Nan::ThrowTypeError("Invalid argument type");
	}

	FANNY *obj = new FANNY(fann);
	obj->Wrap(info.This());
	info.GetReturnValue().Set(info.This());
}

bool FANNY::checkError() {
	unsigned int fannerr = fann->get_errno();
	if (fannerr) {
		std::string errstr = fann->get_errstr();
		std::string msg = std::string("FANN error ") + std::to_string(fannerr) + ": " + errstr;
		Nan::ThrowError(msg.c_str());
		fann->reset_errno();
		fann->reset_errstr();
		return true;
	} else {
		return false;
	}
}

NAN_METHOD(FANNY::save) {
	if (info.Length() != 2) return Nan::ThrowError("Takes a filename and a callback");
	if (!info[0]->IsString() || !info[1]->IsFunction()) return Nan::ThrowTypeError("Wrong argument type");
	std::string filename(*v8::String::Utf8Value(info[0]));
	Nan::Callback *callback = new Nan::Callback(info[1].As<v8::Function>());
	Nan::AsyncQueueWorker(new SaveFileWorker(callback, info.Holder(), filename, false));
}

NAN_METHOD(FANNY::saveToFixed) {
	if (info.Length() != 2) return Nan::ThrowError("Takes a filename and a callback");
	if (!info[0]->IsString() || !info[1]->IsFunction()) return Nan::ThrowTypeError("Wrong argument type");
	std::string filename(*v8::String::Utf8Value(info[0]));
	Nan::Callback *callback = new Nan::Callback(info[1].As<v8::Function>());
	Nan::AsyncQueueWorker(new SaveFileWorker(callback, info.Holder(), filename, true));
}

int FANNY::fannInternalCallback(
	FANN::neural_net &fann,
	FANN::training_data &train,
	unsigned int max_epochs,
	unsigned int epochs_between_reports,
	float desired_error,
	unsigned int epochs,
	void *user_data
) {
	FANNY *fanny = (FANNY *)user_data;

}

NAN_METHOD(FANNY::setCallback) {
	#ifndef FANNY_FIXED
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() == 0 || !info[0]->IsFunction()) {
		fanny->trainingCallbackFn.Reset();
		fanny->fann->set_callback(NULL, NULL);
	} else {
		fanny->trainingCallbackFn.Reset(info[0].As<v8::Function>());
		fanny->fann->set_callback(fannInternalCallback, fanny);
	}
	#else
	Nan::ThrowError("Not supported for fixed FANN");
	#endif
}

void FANNY::_doTrainOrTest(
	const Nan::FunctionCallbackInfo<v8::Value> &info,
	bool fromFile,
	bool isCascade,
	bool singleEpoch,
	bool isTest
) {
	#ifndef FANNY_FIXED
	bool hasConfigParams = !singleEpoch && !isTest;
	unsigned int numArgs = hasConfigParams ? 5 : 2;
	if (info.Length() != numArgs) return Nan::ThrowError("Invalid arguments");
	std::string filename;
	Nan::MaybeLocal<v8::Object> maybeTrainingData;
	if (fromFile) {
		if (!info[0]->IsString()) return Nan::ThrowTypeError("First argument must be a string");
		filename = std::string(*v8::String::Utf8Value(info[0]));
	} else {
		if (!info[0]->IsObject()) return Nan::ThrowTypeError("First argument must be TrainingData");
		if (!Nan::New(TrainingData::constructorFunctionTpl)->HasInstance(info[0])) return Nan::ThrowTypeError("First argument must be TrainingData");
		v8::Local<v8::Object> trainingDataHolder = info[0].As<v8::Object>();
		maybeTrainingData = Nan::MaybeLocal<v8::Object>(trainingDataHolder);
	}
	unsigned int maxIterations = 0;
	unsigned int iterationsBetweenReports = 0;
	float desiredError = 0;
	if (hasConfigParams) {
		if (!info[1]->IsNumber() || !info[2]->IsNumber() || !info[3]->IsNumber()) {
			return Nan::ThrowTypeError("Arguments must be numbers");
		}
		maxIterations = info[1]->Uint32Value();
		iterationsBetweenReports = info[2]->Uint32Value();
		desiredError = (float)info[3]->NumberValue();
	}
	if (!info[numArgs - 1]->IsFunction()) return Nan::ThrowTypeError("Last argument must be callback");
	Nan::Callback *callback = new Nan::Callback(info[numArgs - 1].As<v8::Function>());
	Nan::AsyncQueueWorker(new TrainWorker(
		callback,
		info.Holder(),
		maybeTrainingData,
		fromFile,
		filename,
		isCascade,
		maxIterations,
		iterationsBetweenReports,
		desiredError,
		singleEpoch,
		isTest
	));
	#else
	Nan::ThrowError("Not supported for fixed FANN");
	#endif
}

NAN_METHOD(FANNY::trainEpoch) {
	_doTrainOrTest(info, false, false, true, false);
}

NAN_METHOD(FANNY::trainOnData) {
	_doTrainOrTest(info, false, false, false, false);
}

NAN_METHOD(FANNY::trainOnFile) {
	_doTrainOrTest(info, true, false, false, false);
}

NAN_METHOD(FANNY::cascadetrainOnData) {
	_doTrainOrTest(info, false, true, false, false);
}

NAN_METHOD(FANNY::cascadetrainOnFile) {
	_doTrainOrTest(info, true, true, false, false);
}

NAN_METHOD(FANNY::testData) {
	_doTrainOrTest(info, false, false, true, true);
}

NAN_METHOD(FANNY::run) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 1) return Nan::ThrowError("Takes one argument");
	if (!info[0]->IsArray()) return Nan::ThrowError("Must be array");
	std::vector<fann_type> inputs = v8ArrayToFannData(info[0]);
	if (inputs.size() != fanny->fann->get_num_input()) return Nan::ThrowError("Wrong number of inputs");
	fann_type *outputs = fanny->fann->run(&inputs[0]);
	if (fanny->checkError()) return;
	v8::Local<v8::Value> outputArray = fannDataToV8Array(outputs, fanny->fann->get_num_output());
	info.GetReturnValue().Set(outputArray);
}

NAN_METHOD(FANNY::runAsync) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 2) return Nan::ThrowError("Takes two arguments");
	if (!info[0]->IsArray()) return Nan::ThrowError("First argument must be array");
	if (!info[1]->IsFunction()) return Nan::ThrowError("Second argument must be callback");
	std::vector<fann_type> inputs = v8ArrayToFannData(info[0]);
	if (inputs.size() != fanny->fann->get_num_input()) return Nan::ThrowError("Wrong number of inputs");
	Nan::Callback * callback = new Nan::Callback(info[1].As<v8::Function>());
	Nan::AsyncQueueWorker(new RunWorker(callback, inputs, info.Holder()));
}

NAN_METHOD(FANNY::getNumInput) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_num_input();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getNumOutput) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_num_output();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getTotalNeurons) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_total_neurons();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getTotalConnections) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_total_connections();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getNumLayers) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_num_layers();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getBitFail) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	unsigned int num = fanny->fann->get_bit_fail();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::getMSE) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_MSE();
	info.GetReturnValue().Set(num);
}

// by default 0.7
NAN_METHOD(FANNY::getLearningRate) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_learning_rate();
	info.GetReturnValue().Set(num);
}

// by default -0.0001
NAN_METHOD(FANNY::getQuickPropDecay) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_quickprop_decay();
	info.GetReturnValue().Set(num);
}

// by default 1.75
NAN_METHOD(FANNY::getQuickPropMu) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_quickprop_mu();
	info.GetReturnValue().Set(num);
}

// by default 1.2
NAN_METHOD(FANNY::getRpropIncreaseFactor) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_rprop_increase_factor();
	info.GetReturnValue().Set(num);
}

// by default 0.5
NAN_METHOD(FANNY::getRpropDecreaseFactor) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_rprop_decrease_factor();
	info.GetReturnValue().Set(num);
}

// by default 0.1
NAN_METHOD(FANNY::getRpropDeltaZero) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_rprop_delta_zero();
	info.GetReturnValue().Set(num);
}

// by default 0.0
NAN_METHOD(FANNY::getRpropDeltaMin) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_rprop_delta_min();
	info.GetReturnValue().Set(num);
}

// by default 50.0
NAN_METHOD(FANNY::getRpropDeltaMax) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	float num = fanny->fann->get_rprop_delta_max();
	info.GetReturnValue().Set(num);
}

NAN_METHOD(FANNY::initWeights) {
	if (info.Length() != 1) return Nan::ThrowError("Takes an argument");
	if (!Nan::New(TrainingData::constructorFunctionTpl)->HasInstance(info[0])) {
		return Nan::ThrowError("Argument must be an instance of TrainingData");
	}
	TrainingData *fannyTrainingData = Nan::ObjectWrap::Unwrap<TrainingData>(info[0].As<v8::Object>());
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	return fanny->fann->init_weights(*fannyTrainingData->trainingData);
}

NAN_METHOD(FANNY::getLayerArray) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	std::vector<unsigned int> retArrayVector(fanny->fann->get_num_layers());
	fanny->fann->get_layer_array(&retArrayVector[0]);
	uint32_t size = retArrayVector.size();
	v8::Local<v8::Array> v8Array = Nan::New<v8::Array>(size);
	for (uint32_t idx = 0; idx < size; ++idx) {
		v8::Local<v8::Value> value = Nan::New<v8::Number>(retArrayVector[idx]);
		Nan::Set(v8Array, idx, value);
	}
	info.GetReturnValue().Set(v8Array);
}

NAN_METHOD(FANNY::getBiasArray) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	std::vector<unsigned int> retArrayVector(fanny->fann->get_num_layers());
	fanny->fann->get_bias_array(&retArrayVector[0]);
	uint32_t size = retArrayVector.size();
	v8::Local<v8::Array> v8Array = Nan::New<v8::Array>(size);
	for (uint32_t idx = 0; idx < size; ++idx) {
		v8::Local<v8::Value> value = Nan::New<v8::Number>(retArrayVector[idx]);
		Nan::Set(v8Array, idx, value);
	}
	info.GetReturnValue().Set(v8Array);
}

NAN_METHOD(FANNY::train) {
	#ifndef FANNY_FIXED
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 2) return Nan::ThrowError("Must have 2 arguments: input, desired_output");
	if (!info[0]->IsArray() || !info[1]->IsArray()) return Nan::ThrowError("Argument not an array");

	std::vector<fann_type> input = v8ArrayToFannData(info[0]);
	std::vector<fann_type> desired_output = v8ArrayToFannData(info[1]);

	if (input.size() != fanny->fann->get_num_input()) return Nan::ThrowError("Wrong number of inputs");
	if (desired_output.size() != fanny->fann->get_num_output()) return Nan::ThrowError("Wrong number of desired ouputs");

	fanny->fann->train(&input[0], &desired_output[0]);

	#else
	Nan::ThrowError("Not supported for fixed fann");
	#endif
}

NAN_METHOD(FANNY::test) {
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 2) return Nan::ThrowError("Must have 2 arguments: input, desired_output");
	if (!info[0]->IsArray() || !info[1]->IsArray()) return Nan::ThrowError("Argument not an array");

	std::vector<fann_type> input = v8ArrayToFannData(info[0]);
	std::vector<fann_type> desired_output = v8ArrayToFannData(info[1]);

	if (input.size() != fanny->fann->get_num_input()) return Nan::ThrowError("Wrong number of inputs");
	if (desired_output.size() != fanny->fann->get_num_output()) return Nan::ThrowError("Wrong number of desired ouputs");

	fann_type *outputs = 	fanny->fann->test(&input[0], &desired_output[0]);
	if (fanny->checkError()) return;
	v8::Local<v8::Value> outputArray = fannDataToV8Array(outputs, fanny->fann->get_num_output());
	info.GetReturnValue().Set(outputArray);
}

NAN_METHOD(FANNY::scaleTrain) {
	#ifndef FANNY_FIXED
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 1) return Nan::ThrowError("Must have an argument: tainingData");
	if (!Nan::New(TrainingData::constructorFunctionTpl)->HasInstance(info[0])) {
		return Nan::ThrowError("Argument must be an instance of TrainingData");
	}
	TrainingData *fannyTrainingData = Nan::ObjectWrap::Unwrap<TrainingData>(info[0].As<v8::Object>());
	fanny->fann->scale_train(*fannyTrainingData->trainingData);

	#else
	Nan::ThrowError("Not supported for fixed fann");
	#endif
}

NAN_METHOD(FANNY::setScalingParams) {
	#ifndef FANNY_FIXED
	FANNY *fanny = Nan::ObjectWrap::Unwrap<FANNY>(info.Holder());
	if (info.Length() != 5) return Nan::ThrowError("Must have 5 arguments: tainingData, new_input_min, new_input_max, new_output_min, and new_output_max");
	if (!Nan::New(TrainingData::constructorFunctionTpl)->HasInstance(info[0])) {
		return Nan::ThrowError("Argument must be an instance of TrainingData");
	}

	if (!info[1]->IsNumber() || !info[2]->IsNumber() || !info[3]->IsNumber() || !info[4]->IsNumber()) {
		return Nan::ThrowError("new_input_min, new_input_max, new_output_min, and new_output_max must be of numbers");
	}

	TrainingData *fannyTrainingData = Nan::ObjectWrap::Unwrap<TrainingData>(info[0].As<v8::Object>());

	float new_input_min = info[1]->NumberValue();
	float new_input_max = info[2]->NumberValue();
	float new_output_min = info[3]->NumberValue();
	float new_output_max = info[4]->NumberValue();

	fanny->fann->set_scaling_params(*fannyTrainingData->trainingData, new_input_min, new_input_max, new_output_min, new_output_max);

	#else
	Nan::ThrowError("Not supported for fixed fann");
	#endif
}

}
