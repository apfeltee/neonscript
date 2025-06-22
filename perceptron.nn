// single-layer perceptron (adopted from @github.com:NotAPenguin0/pscript.git)

class AIState
{
    constructor()
    {
        this.weights = [];
        this.bias = 0.0;
        this.learning_rate = 0.0;
    }
};

class Sample
{
    constructor(input, output)
    {
        // input vector for this sample
        this.input = input;
        // desired output for this sample.
        this.output = output;
    }
};

function dot(x, y)
{
    if (x.length != y.length) {
        // TODO: add mechanism for error reporting in language?
        println("size for dot product does not match");
        return -999.0;
    }

    var result = 0.0;
    var n = x.length;
    for (var i = 0; i < n; i += 1) {
        result += x[i] * y[i];
    }
    return result;
}

function list_size(x) {
    return x.length;
}

function ai_output(state, input)
{
    var unbiased = dot(state.weights, input);
    // apply bias to output
    return unbiased + state.bias;
}

function sample_output(state, sample)
{
    return ai_output(state, sample.input);
}

function init_state(in_vec_size, bias, learning_rate)
{
    var state = new AIState();
    state.bias = bias;
    state.learning_rate = learning_rate;
    var weights = state.weights;
    for (var i = 0; i < in_vec_size; i += 1) {
        // Could initialize weight to a random value in the future
        weights.push(0.0);
    }
    return state;
}

// This will create samples trying to make the perceptron classify points with y < 0 as 0, and y > 0 as 1.
function obtain_training_samples()
{
    return [
         new Sample([0.72,0.82],-1.0),
         new Sample([0.91,-0.69],-1.0),
         new Sample([0.03,0.93],-1.0),
         new Sample([0.12,0.25],-1.0),
         new Sample([0.96,0.47],-1.0),
         new Sample([0.8,-0.75],-1.0),
         new Sample([0.46,0.98],-1.0),
         new Sample([0.66,0.24],-1.0),
         new Sample([0.72,-0.15],-1.0),
         new Sample([0.35,0.01],-1.0),
         new Sample([-0.11,0.1],1.0),
         new Sample([0.31,-0.96],1.0),
         new Sample([0.0,-0.26],1.0),
         new Sample([-0.43,-0.65],1.0),
         new Sample([0.57,-0.97],1.0),
         new Sample([-0.72,-0.64],1.0),
         new Sample([-0.25,-0.43],1.0),
         new Sample([-0.12,-0.9],1.0),
         new Sample([-0.58,0.62],1.0),
         new Sample([-0.77,-0.76],1.0)
    ];
}

function print_classification(state, input)
{
    var classification = ai_output(state, input);
    println("classifier for input = ", input, ": ", classification)
}

println("simple single layer perceptron");

var training = obtain_training_samples();
var state = init_state(2, 1.0, 1.0);

var threshold = 0.0;

var max_iterations = 100;
var hits = 0;
var it = 0;
while(it < max_iterations && hits != training.length) {
    hits = 0;

    for (var t = 0; t < training.length; t += 1) {
        var sample = training[t];
        var inputs = sample.input;
        var output = sample_output(state, sample);
        var y = 0.0;

        if (output > threshold) {
            y = 1.0;
        } else {
            y = -1.0;
        }

        // update weights if output does not match expected output
        if (y == sample.output) {
            hits += 1;
        } else {
            var weights = state.weights;
            var n = weights.length;
            for (var j = 0; j < n; j += 1) {
                weights[j] = weights[j] + (state.learning_rate * sample.output * inputs[j]);
            }
            state.bias = state.bias + state.learning_rate * sample.output;
            println("Error - updating weight to ", weights);
        }
    }

    println("Iteration ", it, ": Correct: ", hits, " / ", training.length);
    it += 1;
}

println("Training complete in ", it, " iterations");
