let socket = new WebSocket(`ws://${window.location.hostname}/ws`);
var activeSliders = 0;

let update_rgb_array_length = document.getElementById("update-size");
let global_color_picker = document.getElementById("affect-all");

update_rgb_array_length.addEventListener("click", function(e){
    var number_input = document.getElementById("number-led").value;
    var obj = {
        length: Number(number_input)
    };
    sendObject(obj);

    //changeNumberOfColorPickers(number_input,[[255,246,0],[10,10,10]]);
})

document.addEventListener("DOMContentLoaded", function(){
    var main = document.getElementsByClassName('color-container')[0];
    createColorPicker(main,activeSliders);
});

function apllyAllCheckboxEventHandler(byUser){
    let checked = global_color_picker.checked;
    let colorpickers = getAllColorPickers();
    if(checked){
        for(var i=1; i<activeSliders;i++){
            colorpickers[i].style.display = "none";
        }
    }else{
        for(var i=1; i<activeSliders;i++){
            colorpickers[i].style.display = "";
        }
    }

    if(byUser){
        var obj = {
            affectall: checked ? 1 : 0
        }
        sendObject(obj);
    }
}

global_color_picker.addEventListener("click",function(e){
    apllyAllCheckboxEventHandler(true);
})
global_color_picker.addEventListener("server-update",function(e){
    apllyAllCheckboxEventHandler(false);
})

function createColorPicker(parent,id,createBtn=true){
    /*
        <section class="color-picking-section">
          <div class="color-preview-container">
            <div class="color-preview" id="color-preview-1"></div>
          </div>
          <div class="color-input">
            <div class="slider-container">
                <input class="red-slider" type="range" id="red-slider-1" min="0" max="255" step="1" value="255">
            </div>
            <div class="slider-container">
                <input class="green-slider" type="range" id="green-slider-1" min="0" max="255" step="1" value="255">
            </div>
            <div class="slider-container">
                <input class="blue-slider" type="range" id="blue-slider-1" min="0" max="255" step="1" value="255">
            </div>
            <div class="slider-container">
                <button class="update-lights-btn" id="updateBtn-1">Update Lights</button>
            </div>
          </div>
        </section>
    */
    var section = document.createElement('section');
    section.setAttribute('class',"color-picking-section");
    section.setAttribute('id',"color-picking-section-"+id);

    var divPrevContainer = document.createElement('div');
    divPrevContainer.setAttribute('class',"color-preview-container");
    divPrevContainer.setAttribute('id',"color-preview-container-"+id);

    var divPrev = document.createElement('div');
    divPrev.setAttribute('class',"color-preview");
    divPrev.setAttribute('id',"color-preview-"+id);

    var divColorInput = document.createElement('div');
    divColorInput.setAttribute('class',"color-input");
    divColorInput.setAttribute('id',"color-input-"+id);

    var divRedSliderContainer = document.createElement('div');
    divRedSliderContainer.setAttribute('class',"slider-container");
    divRedSliderContainer.setAttribute('id',"slider-container-red-"+id);

    var inputRedSlider = document.createElement('input');
    inputRedSlider.setAttribute('class',"red-slider");
    inputRedSlider.setAttribute('id',"red-slider-"+id);
    inputRedSlider.setAttribute('type',"range");
    inputRedSlider.setAttribute('min',"0");
    inputRedSlider.setAttribute('max',"255");
    inputRedSlider.setAttribute('step',"1");
    inputRedSlider.setAttribute('value',"0");
    inputRedSlider.addEventListener('input',updateSliderPrevEvent);

    var divGreenSliderContainer = document.createElement('div');
    divGreenSliderContainer.setAttribute('class',"slider-container");
    divGreenSliderContainer.setAttribute('id',"slider-container-green-"+id);

    var inputGreenSlider = document.createElement('input');
    inputGreenSlider.setAttribute('class',"green-slider");
    inputGreenSlider.setAttribute('id',"green-slider-"+id);
    inputGreenSlider.setAttribute('type',"range");
    inputGreenSlider.setAttribute('min',"0");
    inputGreenSlider.setAttribute('max',"255");
    inputGreenSlider.setAttribute('step',"1");
    inputGreenSlider.setAttribute('value',"0");
    inputGreenSlider.addEventListener('input',updateSliderPrevEvent);

    var divBlueSliderContainer = document.createElement('div');
    divBlueSliderContainer.setAttribute('class',"slider-container");
    divBlueSliderContainer.setAttribute('id',"slider-container-blue-"+id);

    var inputBlueSlider = document.createElement('input');
    inputBlueSlider.setAttribute('class',"blue-slider");
    inputBlueSlider.setAttribute('id',"blue-slider-"+id);
    inputBlueSlider.setAttribute('type',"range");
    inputBlueSlider.setAttribute('min',"0");
    inputBlueSlider.setAttribute('max',"255");
    inputBlueSlider.setAttribute('step',"1");
    inputBlueSlider.setAttribute('value',"0");
    inputBlueSlider.addEventListener('input',updateSliderPrevEvent);

    var divButtonContainer = document.createElement('div');
    divButtonContainer.setAttribute('class',"slider-container");
    divButtonContainer.setAttribute('id',"slider-container-btn-"+id);

    var btnButton = document.createElement('input');
    btnButton.setAttribute('class',"update-lights-btn");
    btnButton.setAttribute('id',"updateBtn-"+id);
    btnButton.setAttribute('readonly',"readonly");
    btnButton.value = "Update";
    btnButton.addEventListener('click',updateLightsEvent);

    section.appendChild(divPrevContainer);
    section.appendChild(divColorInput);

    divPrevContainer.appendChild(divPrev);

    divColorInput.appendChild(divRedSliderContainer);
    divColorInput.appendChild(divGreenSliderContainer);
    divColorInput.appendChild(divBlueSliderContainer);
    if(createBtn) {
        divColorInput.appendChild(divButtonContainer);
    }
    

    divRedSliderContainer.appendChild(inputRedSlider);
    divGreenSliderContainer.appendChild(inputGreenSlider);
    divBlueSliderContainer.appendChild(inputBlueSlider);
    if(createBtn){
        divButtonContainer.appendChild(btnButton);
    }
    
    parent.appendChild(section);
    activeSliders++;
}


function updateLightsEvent(e){
    var id = e.target.id;
    var idNum = getLastNumberFromString(id);

    var color = getSliderColors(idNum,true);

    var affect_all = global_color_picker.checked;

    var obj = {lights:{
        affected: affect_all==true ? -1 : Number(idNum),
        color: {
            r: color[0],
            g: color[1],
            b: color[2]
        }
    }};
    sendObject(obj);
}

function getAllColorPickers(){
    var elements = Array.apply(null,Array(activeSliders));
    for (let i = 0; i < activeSliders; i++) {
        elements[i] = document.getElementById("color-picking-section-"+i);
    }
    return elements;
}

function changeNumberOfColorPickers(number,colors){
    if(number == activeSliders){
        return;
    }
    var old = getAllColorPickers();
    old.forEach(element => {
        element.remove();
        activeSliders--;
    });
    var main = document.getElementsByClassName('color-container')[0];
    for (let i = 0; i < number; i++) {
        createColorPicker(main,activeSliders);
    }

    for (let i = 0; i < number; i++) {
        var color = [0,0,0];
        if (i < colors.length) {
            color = colors[i];
        }
        setSliderColors(i,color);
    }
}

function updateSliderPrevEvent(e){
    var sliderValue = e.target.value;
    var sliderID = e.target.id;
    var idNum = getLastNumberFromString(sliderID);
    var caller = document.getElementById(e.target.id);
    var prev = document.getElementById("color-preview-"+idNum);

    if(sliderID.includes("red")){
        var color = `rgba(${sliderValue}, ${0}, ${0}, ${1})`;
        caller.style.setProperty('--thumb-color', color);
    }
    if(sliderID.includes("green")){
        var color = `rgba(${0}, ${sliderValue}, ${0}, ${1})`;
        caller.style.setProperty('--thumb-color', color);
    }
    if(sliderID.includes("blue")){
        var color = `rgba(${0}, ${0}, ${sliderValue}, ${1})`;
        caller.style.setProperty('--thumb-color', color);
    }

    prev.style.setProperty('--bkg-color',getSliderColors(idNum));
}

function getSliderColors(id, array=false){
    var ids = [ "red-slider-"+id,"green-slider-"+id,"blue-slider-"+id];
    var elements = Array.apply(null,Array(3));

    for(var i=0; i<3;i++){
      elements[i] = document.getElementById(ids[i]);
    }

    if(!array){
        var color = `rgba(${elements[0].value}, ${elements[1].value}, ${elements[2].value}, ${1})`
        return color;
    }else{
        return [Number(elements[0].value),Number(elements[1].value),Number(elements[2].value), 1];
    }
}

function setSliderColors(id,color){
    var ids = [ "red-slider-"+id,"green-slider-"+id,"blue-slider-"+id];
    var elements = Array.apply(null,Array(3));

    for(var i=0; i<3;i++){
      elements[i] = document.getElementById(ids[i]);
    }

    elements[0].value = color[0];//r
    elements[1].value = color[1];//g
    elements[2].value = color[2];//b

    for(var i=0; i<3;i++){
        elements[i].dispatchEvent(new Event('input',{
            bubbles: true,
            cancelable: true,
        }));
      }
}

socket.onmessage = function(event){
    let message = event.data;
    var parsedData;
    try{
        parsedData = JSON.parse(message);
        console.log("[IN] <- " + message);
    }catch(exception){
        console.log("received: " + message + " failed to decode JSON: " + exception);
        return;
    }
    
    //{"input":{"affected":-1,"color":{"r":0,"g",0,"b":0}}}
    if(parsedData.hasOwnProperty('input')){
        if(parsedData.input.affected == -1){
            var color = Array.apply(null,Array(3));
            color[0] = parsedData.input.color[0];
            color[1] = parsedData.input.color[1];
            color[2] = parsedData.input.color[2];

            for(var i=0; i<activeSliders; i++){
                setSliderColors(i,color);
            }
        }else{
            setSliderColors(parsedData.input.affected,parsedData.input.color);
        }
    }
    //"{"resized":{"length":2,"colors":[[0,0,0],[0,0,0],[255,255,0],[60,80,50]]}}"
    else if(parsedData.hasOwnProperty('resized')){
        var length = parsedData.resized.length;
        var colors = parsedData.resized.colors;

        changeNumberOfColorPickers(length,colors);

        var numberTxtBx = document.getElementById("number-led");
        numberTxtBx.value = length;
    //{"sync":{"apply_all":false,"length":1,"colors":[0,0,0]}}
    }else if(parsedData.hasOwnProperty('sync')){//info to set up site how others are currently using it
        var length = parsedData.sync.length;
        var colors = parsedData.sync.colors;

        changeNumberOfColorPickers(length,colors);
        if(global_color_picker.checked != parsedData.sync.affect_all){

            global_color_picker.checked = parsedData.sync.affect_all;
            global_color_picker.dispatchEvent(new Event('server-update',{
                bubbles: true,
                cancelable: true,
            }));
        }
        var numberTxtBx = document.getElementById("number-led");
        numberTxtBx.value = length;
    }
    else if(parsedData.hasOwnProperty('affect_all')) {
        if(global_color_picker.checked != parsedData.affect_all){
            global_color_picker.checked = parsedData.affect_all;
            global_color_picker.dispatchEvent(new Event('server-update',{
                bubbles: true,
                cancelable: true,
            }));
        }
    }
    else{
        console.log("Unkownmessage Received: "+message);
    }
}

function sendObject(object){
    var jsonString = JSON.stringify(object);

    if(socket.readyState == socket.OPEN && socket != null){
        socket.send(jsonString);
    }
    console.log("[OUT] -> "+jsonString);
}

function getLastNumberFromString(str) {
    const matches = str.match(/\d+$/); // Match one or more digits at the end of the string
    if (matches) {
      return parseInt(matches[0]); // Parse the matched digits as an integer
    } else {
      return null; // No number found
    }
}