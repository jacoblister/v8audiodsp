function start(sampleRate) {
    console.log('called start');
}

function process(samples) {
    for(var i = samples.length; i--;) {
        samples[i] *= 10;
    }
}