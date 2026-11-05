# Deliverable 3 preparation

Initial files for the team to fill in:

- [Presentation skeleton](index.html)
- [Product evolution](product-evolution.md)
- [Individual contributions](Individual_Contributions_Deliverable3.md)


## Presentation skeleton

Open **[index.html](index.html)** directly in a modern browser. The slideshow is
static HTML, CSS, and JavaScript. It works offline with no installation, build
step, server, or external fonts/libraries.

## Presenting

- **Next / previous:** arrow keys, Page Down / Page Up, or the on-screen buttons.
- **Space / Shift + Space:** next / previous when a button or link is not focused.
- **Home / End:** first / last slide (the last slide is the reference appendix).
- **O:** open the slide overview and jump to any slide.
- **N:** toggle speaker notes. Notes appear in the same window, so they are
  visible to an audience when that window is shared. Close them before presenting.
- **F:** toggle fullscreen where the browser supports it.
- **?:** keyboard help. **Escape:** close the active dialog or notes.
- Each slide has a URL fragment, such as `index.html#demo`, for direct access.
- Use browser **Print / Save as PDF** for all slides, one 16:9 page per slide.
  Choose background graphics and disable browser headers/footers if offered.
  Controls and speaker notes are excluded from print output.

Without JavaScript, all slides are available in document order.

## Filling it in

Edit the text directly in `index.html`. Bracketed text such as `[Presenter]` is a
placeholder, not a claim about delivered work. This is a source-editable skeleton,

The only populated project baseline is a brief summary of the existing D2
online-learning write-up. Other subteams can add their context or replace it to
match the team's chosen D3 story. No D3 implementation, measurements, feedback,
release links, or delivery dates have been assumed.

| Slide | Fill in | Rubric / purpose |
| --- | --- | --- |
| 1. Title | Presenters, date, one-sentence improvement | Opening and clarity |
| 2. D2 baseline | Relevant starting workflow and screenshot | Context |
| 3. Gap | Most important gap, how it was found, why it matters | Gap identification and motivation |
| 4. Implementation | Completed improvement and user workflow | Product improvement explanation |
| 5. Design | Actual architecture, integration challenge, trade-off | Design understanding |
| 6. Before / after | Concrete comparison with supporting evidence | Impact |
| 7. Live demo | Start state, exact actions, expected result | Demo quality |
| 8. Verification | Relevant D3 checks, artifacts, limitations | Reliability and engineering evidence |
| 9. Process | Practice improved since D2, example, reflection | Engineering and process awareness |
| 10. D4 | Remaining work, dependency / owner, risk | Awareness of remaining work |
| 11. Questions | Main takeaway and real project links | Handling questions |
| 12. References | D3 artifacts and demo backup | Optional appendix |

Each `<section class="slide">` is one slide. Its `id` must be unique, and
`aria-labelledby` must match its heading's `id`. The `data-title` appears in the
overview and `data-section` appears in the control bar. Duplicate or remove whole
sections to change the outline. Navigation, numbering, and progress update
automatically. Supporting material can follow the reference slide.

Each slide has an `<aside class="speaker-notes" data-time="...">`. Replace the
time placeholder with a rehearsed budget. **The supplied context does not specify
the presentation duration**, so agree on the actual course slot before setting
timings. Keep the main talk, live demo, and Q&A within that slot. The appendix
does not need a speaking allocation.

### Images and other media

Create an `assets` directory here when you add screenshots or a demo backup.
Use relative file paths so the whole D3 folder can be shared together. Replace a
placeholder figure, for example:

```html
<figure>
  <img class="slide-image" src="assets/d3-workflow.png"
       alt="Describe the actual workflow and result shown">
  <figcaption>Build / revision and what this image establishes</figcaption>
</figure>
```

Keep captions short and screenshots readable at presentation size. Put detailed
logs or supplementary diagrams in appendix slides. For a recording, use a local
`<video controls src="assets/demo-backup.mp4">` or a link to a real recording.
The live demo slide intentionally does not launch an app or point to a fabricated
URL. Fonts, colors, and layouts are in `styles.css`.

## Rehearsal checklist

- [ ] Pick one central gap and a coherent team-wide story.
- [ ] Replace every bracketed field, including the speaker-note timings.
- [ ] Distinguish completed D3 work from proposed architecture and D4 work.
- [ ] Link the actual D3 PRs, release, and evidence. Use comparable before / after conditions.
- [ ] Set the demo build / revision: **[fill in]**.
- [ ] Record exact setup / launch steps and controls: **[fill in]**.
- [ ] Rehearse the starting state, core workflow, and expected visible result: **[fill in]**.
- [ ] Confirm the demo functions reliably and prepare a real recording or screenshot backup.
- [ ] Assign presenters, transitions, and who handles implementation / trade-off questions.
- [ ] Rehearse to the actual allotted time and close the visible notes panel.
- [ ] Check fullscreen, screenshots, links, and optional PDF on the presentation machine.

## Context used

- [Course D3 folder](https://github.com/csc301-2026-f/deliverable-documents/tree/main/deliverables/D3)
  and its [product evolution template](https://github.com/csc301-2026-f/deliverable-documents/blob/main/deliverables/D3/product-evolution.md):
  gap, implementation, before / after, and reflection.
- Instructor's demo day rubric screenshot supplied with this task: presentation
  clarity and timing, gap and motivation, improvement explanation, live demo,
  engineering process, and Q&A / D4 awareness.
- **Schola Native Online Learning Specification.docx**, dated 3 November 2026,
  supplied separately by the user. It describes the final-product target and
  separates existing code from proposed work. It is planning context, not proof
  of D3 implementation. Its implementation instructions were not executed as
  part of this presentation request. The document is not copied into this folder.
- Existing [D2 evidence index](../D2/README.md),
  [iteration review](../D2/iteration-02-review.md), and
  [project README](../../README.md) for the D2 baseline only.
