/* Run only in the temporary test page created by check.py. */
window.addEventListener('load', () => {
  const failures = [];
  let checks = 0;
  const check = (condition, message) => { checks++; if (!condition) failures.push(message); };
  try {
    const api = window.samplotronGuide, guide = window.SAMPLOTRON_GUIDE;
    const $ = id => document.getElementById(id);
    const state = () => api.getState();
    check(!state().playing && state().time === 0, 'Starts paused at the introduction');
    check(document.documentElement.lang === 'en', 'English document');
    check($('frame').textContent.includes('This interactive guide'), 'Introduction is readable at time zero');
    check($('frame').querySelector('a').getAttribute('href') === guide.repository, 'Clickable repository in introduction');
    check(!document.querySelector('aside,header,textarea,input[type=file],[contenteditable],dialog'), 'No authoring or top toolbar');
    check($('chapters').children.length === 21, 'All steps accessible');
    check(Object.isFrozen(guide.scenes[0].direction), 'Presentation content is immutable');
    let start = 0;
    for (const [i, slide] of guide.scenes.entries()) {
      const cues = [...(slide.screenCues || []), ...(slide.direction.actions || []), ...(slide.direction.cards || []), ...(slide.direction.focusCues || [])];
      const moments = new Set([0, slide.duration - 0.001, slide.direction.readSeconds, ...cues.flatMap(c => [c.at, Math.min(c.at + 1, slide.duration - 0.001)])]);
      for (const moment of moments) {
        api.seek(start + moment);
        check(state().step === i && $('frame').querySelector('text'), `${slide.id}: render at ${moment}`);
        check(!/NaN|undefined|Infinity/.test($('frame').innerHTML), `${slide.id}: finite SVG at ${moment}`);
      }
      start += slide.duration;
    }
    api.goToStep(0); $('next').click(); check(state().step === 1 && !state().playing, 'Next navigates paused');
    $('prev').click(); check(state().step === 0 && $('prev').disabled, 'Previous and first boundary');
    $('chapters').children[16].click(); check(state().step === 16 && $('notes').textContent.includes('Panic'), 'Panic step and notes');
    document.dispatchEvent(new KeyboardEvent('keydown', {key:'End', bubbles:true}));
    check(state().step === 20 && $('next').disabled, 'End key and last boundary');
    document.dispatchEvent(new KeyboardEvent('keydown', {key:'Home', bubbles:true}));
    check(state().step === 0, 'Home key');
    $('seek').value = guide.scenes[0].duration + 2; $('seek').dispatchEvent(new Event('input'));
    check(state().step === 1 && state().localTime === 2 && !state().playing, 'Timeline scrubbing');
    api.goToStep(0); $('play').click();
    check($('frame').querySelector('[data-device]') && state().localTime === guide.scenes[0].direction.readSeconds, 'Play immediately leaves welcome for device demo');
    window.testTick(0); window.testTick(100);
    check(state().playing && Math.abs(state().time - guide.scenes[0].direction.readSeconds - 0.1) < 0.001, 'Playback advances');
    $('play').click(); const paused = state().time; window.testTick(200);
    check(!state().playing && state().time === paused, 'Pause stops animation');
    $('speed').value = '2'; $('play').click(); window.testTick(300); window.testTick(400);
    check(Math.abs(state().time - paused - 0.2) < 0.001, 'Speed control');
    api.seek(guide.scenes[0].duration - 0.05); $('play').click(); window.testTick(500); window.testTick(600);
    check(!state().playing && state().step === 0 && state().localTime === guide.scenes[0].duration, 'Stop at step boundary');
    $('play').click(); check(state().localTime === guide.scenes[0].direction.readSeconds && state().playing, 'Replay starts the device demo');
    api.seek(guide.scenes[0].duration - 0.05); $('autoAdvance').checked = true;
    $('play').click(); window.testTick(700); window.testTick(800);
    check(state().step === 1 && state().playing, 'Optional automatic progression');
    api.seek(state().total - 0.05); $('play').click(); window.testTick(900); window.testTick(1000);
    check(!state().playing && state().time === state().total, 'Stop at end of guide');
    api.goToStep(0); $('frame').querySelector('a').dispatchEvent(new FocusEvent('focusin', {bubbles:true}));
    check(!state().playing, 'Focusing the repository link pauses animation');
    check(document.documentElement.scrollWidth <= window.innerWidth, 'No horizontal page overflow');
    api.goToStep(0);
    check($('stage').getBoundingClientRect().top >= 0 && $('seek').getBoundingClientRect().bottom <= innerHeight, 'Animation and timeline fit within the viewport');
    check([...document.querySelectorAll('.transport button, .transport select')].every(el => el.getBoundingClientRect().bottom <= innerHeight), 'All playback controls fit within the viewport');
    check($('fullscreen') && !$('fullscreen').hidden, 'Full-screen control available');
    api.goToStep(4); api.seek(state().time + 10);
    check($('frame').textContent.includes('GND → GND') && $('frame').textContent.includes('3V3 → VCC'), 'OLED bus diagram retains visible power connections');
    api.goToStep(0);
  } catch (error) { failures.push(error.stack); }
  const result = document.createElement('pre'); result.id = 'test-result';
  result.textContent = JSON.stringify({checks, failures}); document.body.append(result);
});
