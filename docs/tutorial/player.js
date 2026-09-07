/* Read-only playback. No authoring controls, file uploads or persistent state. */
(() => {
  'use strict';
  const guide = window.SAMPLOTRON_GUIDE;
  const $ = id => document.getElementById(id);
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
  const stamp = seconds => `${Math.floor(seconds / 60).toString().padStart(2, '0')}:${Math.floor(seconds % 60).toString().padStart(2, '0')}`;
  const starts = [];
  let total = 0;
  for (const slide of guide.scenes) { starts.push(total); total += slide.duration; }
  let current = 0, time = 0, playing = false, lastFrame = null, animation = null;

  function freeze(value) {
    for (const child of Object.values(value)) if (child && typeof child === 'object') freeze(child);
    return Object.freeze(value);
  }
  freeze(guide);
  const end = () => starts[current] + guide.scenes[current].duration;

  function render() {
    const slide = guide.scenes[current];
    $('frame').innerHTML = window.GuideRenderer.render(slide, time - starts[current], current);
    $('frame').setAttribute('aria-label', `${slide.title}. ${slide.caption}`);
    $('clock').textContent = `${stamp(time)} / ${stamp(total)}`;
    $('seek').value = time;
    $('seek').setAttribute('aria-valuetext', `Step ${current + 1}, ${stamp(time - starts[current])} of ${stamp(slide.duration)}`);
    $('play').textContent = playing ? 'Ⅱ Pause' : time >= end() ? '↻ Replay step' : '▶ Play';
    $('play').setAttribute('aria-pressed', String(playing));
  }

  function updateStep() {
    const slide = guide.scenes[current];
    $('prev').disabled = current === 0;
    $('next').disabled = current === guide.scenes.length - 1;
    $('stepStatus').textContent = `Step ${current + 1} of ${guide.scenes.length} · ${slide.title}`;
    $('instructions').textContent = slide.caption;
    $('notes').textContent = slide.note;
    for (const [index, button] of [...$('chapters').children].entries()) {
      if (index === current) button.setAttribute('aria-current', 'step');
      else button.removeAttribute('aria-current');
    }
    $('stepLinks').replaceChildren();
    const links = slide.id === 'intro' ? [{ label: 'Samplotron repository', url: guide.repository }] :
      (slide.direction.cards || []).filter(card => card.link).map(card => ({ label: 'Download firmware from GitHub', url: card.link }));
    for (const { label, url } of links) {
      const link = document.createElement('a');
      link.textContent = label; link.href = url; link.target = '_blank'; link.rel = 'noopener noreferrer';
      $('stepLinks').append(link);
    }
  }

  function pause() {
    playing = false;
    if (animation !== null) cancelAnimationFrame(animation);
    animation = null; lastFrame = null;
    // Do not recreate the SVG here: a focused repository link must keep focus.
    $('play').textContent = time >= end() ? '↻ Replay step' : '▶ Play';
    $('play').setAttribute('aria-pressed', 'false');
  }

  function seek(value) {
    if (!Number.isFinite(value)) return;
    time = clamp(value, 0, total);
    let index = 0;
    while (index < starts.length - 1 && time >= starts[index + 1]) index++;
    if (index !== current) { current = index; updateStep(); }
    render();
  }

  function goToStep(index) {
    if (!Number.isFinite(index)) return;
    pause(); current = clamp(Math.floor(index), 0, guide.scenes.length - 1);
    time = starts[current]; updateStep(); render();
  }

  function tick(now) {
    if (!playing) return;
    if (lastFrame !== null) {
      const nextTime = time + Math.min((now - lastFrame) / 1000, 0.25) * Number($('speed').value);
      if (nextTime >= end() && !$('autoAdvance').checked) {
        time = end(); pause(); render(); return;
      }
      seek(nextTime);
      if (time >= total) { pause(); return; }
    }
    lastFrame = now; animation = requestAnimationFrame(tick);
  }

  function toggle() {
    if (playing) { pause(); return; }
    if (time >= end()) time = starts[current];
    // The welcome text is read while paused. Play immediately starts the demo.
    const direction = guide.scenes[current].direction;
    if (direction.introduction && time - starts[current] < direction.readSeconds) {
      time = starts[current] + direction.readSeconds;
    }
    playing = true; lastFrame = null; render(); animation = requestAnimationFrame(tick);
  }

  for (const [index, slide] of guide.scenes.entries()) {
    const button = document.createElement('button'); button.type = 'button';
    const number = document.createElement('b'); number.textContent = `${String(index + 1).padStart(2, '0')} · ${stamp(starts[index])}`;
    button.append(number, document.createTextNode(slide.title));
    button.addEventListener('click', () => goToStep(index)); $('chapters').append(button);
  }
  $('seek').max = total;
  $('prev').addEventListener('click', () => goToStep(current - 1));
  $('next').addEventListener('click', () => goToStep(current + 1));
  $('play').addEventListener('click', toggle);
  const presentation = document.querySelector('.presentation');
  $('fullscreen').hidden = !document.fullscreenEnabled;
  $('fullscreen').addEventListener('click', async () => {
    $('fullscreenError').hidden = true;
    try {
      if (document.fullscreenElement) await document.exitFullscreen();
      else await presentation.requestFullscreen();
    } catch {
      $('fullscreenError').textContent = 'Full screen is unavailable. Please try again or use your browser’s full-screen command.';
      $('fullscreenError').hidden = false;
    }
  });
  document.addEventListener('fullscreenchange', () => {
    const active = document.fullscreenElement === presentation;
    $('fullscreen').textContent = active ? '⛶ Exit full screen' : '⛶ Full screen';
    $('fullscreen').setAttribute('aria-pressed', String(active));
  });
  $('seek').addEventListener('input', event => { pause(); seek(Number(event.target.value)); });
  $('frame').addEventListener('focusin', event => { if (event.target.closest('a')) pause(); });
  $('frame').addEventListener('pointerdown', event => { if (event.target.closest('a')) pause(); });
  document.addEventListener('visibilitychange', () => { if (document.hidden) pause(); });
  document.addEventListener('keydown', event => {
    if (event.altKey || event.ctrlKey || event.metaKey || /^(INPUT|SELECT|TEXTAREA)$/.test(event.target.tagName)) return;
    if (event.code === 'Space' && !event.target.closest('button,a,summary')) { event.preventDefault(); toggle(); }
    if (event.key === 'ArrowRight') { event.preventDefault(); goToStep(current + 1); }
    if (event.key === 'ArrowLeft') { event.preventDefault(); goToStep(current - 1); }
    if (event.key === 'Home') { event.preventDefault(); goToStep(0); }
    if (event.key === 'End') { event.preventDefault(); goToStep(guide.scenes.length - 1); }
  });

  // Optional deep links select a paused step; no presentation content is changed.
  const params = new URLSearchParams(location.search);
  const index = guide.scenes.findIndex(slide => slide.id === params.get('scene'));
  if (index >= 0) current = index;
  const offset = Number(params.get('t') || 0);
  time = starts[current] + (Number.isFinite(offset) ? clamp(offset, 0, guide.scenes[current].duration) : 0);
  updateStep(); render();
  window.samplotronGuide = Object.freeze({
    goToStep, pause,
    seek: value => { pause(); seek(value); },
    getState: () => ({ step: current, time, playing, total, localTime: time - starts[current] })
  });
})();
