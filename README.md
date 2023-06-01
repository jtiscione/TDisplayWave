# T-DISPLAY S3 WAVE EQUATION SIMULATOR

### THE WAVE EQUATION IN TWO DIMENSIONS

This is a C++ visualization of the scalar wave equation in two dimensions:

u<sub>tt</sub> = c<sup>2</sup> (u<sub>xx</sub> + u<sub>yy</sub>)

where u is the height or amplitude of the 2D wave medium across the screen, u<sub>tt</sub> is its second partial derivative with respect to time, u<sub>xx</sub> and u<sub>yy</sub> are its second partial derivatives with respect to x and y, and c is the wave speed, which may vary with location.

To account for damping, so that waves lose their intensity over time as they travel, the wave equation must incorporate an extra term:

u<sub>tt</sub> = c<sup>2</sup> (u<sub>xx</sub> + u<sub>yy</sub>) - *k* u<sub>t</sub>

where u<sub>t</sub> is the first partial derivative with respect to time, and *k* is a constant which may also vary with location. Large values of *k* produce regions that attenuate waves over short distances, while regions where *k* = 0 are completely transparent and allow waves to continue indefinitely with no dimunition.

The value of u is rendered on the T-Display S3 as a color (in the 16 bit color encoding used in these displays). Values of 0 are rendered as black; positive and negative values are rendered as two separate hues. The system can render about 13 frames per second on its 320 x 170 display.

If the T-Display S3 is the touch version, the user can create waves by touching the screen and dragging.

### IMPLEMENTATION

Since no enhanced precision is necessary at any point for values close to zero, we can use fixed-point arithmetic using 32 bit integers, instead of floating point which would be slower. Wave amplitudes are calibrated to take half the INT32 range, setting aside one bit for overflow.

To perform the calculation we maintain two arrays u and v of fixed-point values that span the pixels in the image. The value of u is the wave amplitude for a given pixel at a given point in time, and v is the rate at which u is changing (i.e. u<sub>t</sub>, the first partial derivative with respect to time).

Looping through all pixels, the value of u<sub>xx</sub> can be approximated at each pixel by comparing it to the average value of the neighboring pixels to the left and right, using the formula:

u<sub>xx</sub> = u<sub>left</sub> + u<sub>right</sub> - 2u

Similarly u<sub>yy</sub> can be approximated at each pixel by comparing to the average value of the neighboring pixels in the perpendicular direction:

u<sub>yy</sub>= u<sub>top</sub> + u<sub>bottom</sub> - 2u

By multiplying the sum u<sub>xx</sub> + u<sub>yy</sub> by our choice of c<sup>2</sup>, we can get u<sub>tt</sub>, the rate at which v changes per unit of time, and add it to v.

At this point we can simulate damping, by shifting v right a certain number of bits and subtracting it from itself, effectively multiplying v by a fraction like 4095/4096 (for low damping) or 31/32 (for high damping) during each loop iteration.

In the second loop we update u based on the value of v. For regions with no impedance (as in air or a vacuum, where c = 1) we simply add v to u. For regions with high impedance (as in glass, where c = 0.5) we add v / 4 to u. Then we select a color for the pixel based on the value of u.

The color values are written into an image array that is then drawn onto a full screen sprite.

#### BOUNDARY CONDITIONS

At the edges we cannot calculate using the standard wave equation, because we don't have all four neighboring pixels. Pixels along the edges are therefore designated as "wall" pixels that remain fixed at u=0. This creates reflective boundaries along the edges that reflect waves inward; this technique also works in the interior region for implementing mirrors.

(It is surprisingly difficult to implement a boundary condition that absorbs a wave with no unwanted reflection at all, as if the medium extends indefinitely in all directions beyond the edges of the screen with infinite computing resources devoted to it. The easiest approach is to set up an absorbant region around the edges wide enough to extinguish the wave. This works reasonably well, but not perfectly. For no reflection to occur at all, the damping must be carefully tuned for the frequency, and it have a gradual onset with no sharp boundaries, increasing as the wave approaches the edges.)

#### WAVE ORIGINS

Wave energy is injected into the medium by one of two mechanisms-
- Pixels can be designated as source pixels that have u set to large oscillating values.
- The user can drag a finger across pixels and introduce large values into v. This is obviously an option on the touch version only.

### PROGRAM MODES

Pressing the left button cycles through different modes available-

- TOUCH_ONLY_MODE (default on touch version): Waves must be created by the user touching the screen
- RANDOM_POINTS_MODE (default on non-touch version): Waves are emitted from several random points
- RANDOM_POINTS_ABSORBER_MODE: Same, with an aborbant region around the edges
- RANDOM_POINTS_MULTIFREQUENCY_MODE: Waves having several different frequencies are emitted from several random points
- RANDOM_POINTS_MULTIFREQUENCY_ABSORBER_MODE: Same, with an absorbant region around the edges
- MONOPOLE_MODE: Waves are emitted from a single point in the center
- MONOPOLE_ABSORBER_MODE: Same, with an aborbant region around the edges
- DIPOLE_MODE: Waves are emitted from a dipole
- DIPOLE_ABSORBER_MODE: Same, with an aborbant region around the edges
- QUADRUPOLE_MODE: Waves are emitted from a dipole
- QUADRUPOLE_ABSORBER_MODE: Same, with an aborbant region around the edges
- SUPERPOSITION_MODE: Two waves having different frequencies are overlapped
- FLAT_MIRROR_MODE: Plane waves reflect off a mirror and strike an absorbant region around the edges
- PARABOLIC_MIRROR_MODE: Plane waves are focused by a parabolic mirror
- ELLIPTIC_MIRROR_MODE: Waves are emitted from one focus and travel to the other
- REFRACTION_MODE: Plane waves travel through a glass pane, striking it at an angle and changing direction at both interfaces in accordance with Snell's Law
- PRISM_MODE: Plane waves travel through a prism
- LENS_MODE: Plane waves are focused by a lens (which does not work that well since its focal length is comparable to the wavelength)
- PARTIAL_INTERNAL_REFLECTION_MODE: Plane waves emerging from glass are partially reflected back into it at its edge
- TOTAL_INTERNAL_REFLECTION_MODE: Plane waves traveling from glass at a shallower angle are completely reflected back, with no transmission across the boundary
- FIBER_OPTIC_MODE: Plane waves are channeled through fiber optic cables using total internal reflection
- WAVEGUIDE_MODE: Plane waves are channeled by mirrors
- PHASED_ARRAY_MODE: Plane waves are generated by a phased array (source pixel amplitudes are not all in sync and depend on position)
- DOUBLE_SLIT_DIFFRACTION_MODE: Plane waves emerge from two slits and interfere with each other
- DIFFRACTION_GRATING_MODE: Plane waves emerge from a diffraction grating and interfere with each other
- MAZE_MODE: Waves travel through a zig-zag maze

The left button also changes the color scale being used.

Pressing the right button resets the current mode to its initial condition, and also changes the color scale.