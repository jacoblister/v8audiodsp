function start(sampleRate) {
    console.log('called start');
}

function process(samples) {
    for(var i = samples.length; i--;) {
        samples[i] *= 10;
    }
}

function dummyLoad(max) {
    console.log('dummy load start');

    var result = 0;
    for(var i = 100000000; i--;) {
        result += i;
    }

    console.log('dummy load end');
}