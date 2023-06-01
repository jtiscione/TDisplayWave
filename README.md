# T-DISPLAY S3 WAVE EQUATION SIMULATOR

### THE WAVE EQUATION IN TWO DIMENSIONS

This is a C++ simulation of the scalar wave equation in two dimensions:

u~tt~ = c^2^ (u~xx~ + u~yy~)

where u is the height or amplitude of the wave medium, u~tt~ is its second partial derivative with respect to time, u~xx~ and u~yy~ are its second partial derivatives with respect to x and y, and c is the wave speed, which may vary with location.

To account for damping, so that waves lose their intensity over time as they travel, the wave equation must incorporate an extra term:

u~tt~ = c^2^ (u~xx~ + u~yy~) - *k* u~t~

where u~t~ is the first partial derivative with respect to time, and *k* is a constant which may also vary with location. Large values of *k* produce regions that attenuate waves over short distances, while regions where *k*=0 are completely transparent and allow waves to continue indefinitely.

### IMPLEMENTATION

Since no enhanced precision is necessary here for values close to zero, we can use fixed-point arithmetic using 32 bit integers, instead of floating point. Wave amplitudes are allowed to take half the INT32 range, setting aside one bit for overflow.

To perform the calculation we maintain two arrays u and v of fixed-point values that span the pixels in the image. The value of u is the wave amplitude for a given pixel at a given point in time, and v is the rate at which u is changing (i.e. u~t~, the first partial derivative with respect to time).

Looping through all pixels, the value of u~xx~ can be approximated at each pixel by comparing it to the average value of the neighboring pixels to the left and right, using the formula:

u~xx~ = u~j-1~ + u~j+1~ - 2u~j~

Similarly u~yy~ can be approximated at each pixel by comparing to the average value of the neighboring pixels in the perpendicular direction:

u~yy~ = u~i-1~ + u~i+1~ - 2u~i~

By multiplying the sum u~xx~ + u~yy~ by our choice of c^2^, we can get u~tt~, the rate at which v changes per unit of time, and add it to v.

At this point we can simulate damping, by shifting v right a certain number of bits and subtracting it from itself, effectively multiplying v by a fraction like 4095/4096 (for low damping) or 31/32 (for high damping) during each loop iteration.

In the second loop we update u based on the value of v (either by adding v or a fixed fraction of v), and then select a color for the pixel based on the value of u.

The color values are written into an image array that is drawn on a full screen sprite. On a T-Display S3 with 320x170 pixels, the simulation runs at about 13 frames per second.

#### BOUNDARY CONDITIONS

At the edges we cannot do this calculation, because we don't have four neighboring pixels. Pixels along the edges are therefore designated as "wall" pixels that remain fixed at u=0. This creates reflective boundaries that reflect waves inward. (It is surprisingly difficult to implement a boundary condition that absorbs a wave with no unwanted reflection at all; the best approach is to set up an absorbent region around the edges wide enough to extinguish the wave.)

#### WAVE ORIGINS

Wave energy is created by one of two mechanisms-
- Pixels can be designated as source pixels that have u set to large oscillating values.
- The user can drag a finger across pixels and introduce large values into v. This is obviously an option on the touch version only.

### PROGRAM MODES

Pressing the left button cycles through different modes available-
- TOUCH_ONLY_MODE (default on touch version)
- RANDOM_POINTS_MODE (default on non-touch version)
- RANDOM_POINTS_ABSORBER_MODE
- RANDOM_POINTS_MULTIFREQUENCY_MODE
- RANDOM_POINTS_MULTIFREQUENCY_ABSORBER_MODE
- MONOPOLE_MODE
- MONOPOLE_ABSORBER_MODE
- DIPOLE_MODE
- DIPOLE_ABSORBER_MODE
- QUADRUPOLE_MODE
- QUADRUPOLE_ABSORBER_MODE
- SUPERPOSITION_MODE
- FLAT_MIRROR_MODE
- PARABOLIC_MIRROR_MODE
- ELLIPTIC_MIRROR_MODE
- REFRACTION_MODE
- PRISM_MODE
- LENS_MODE
- PARTIAL_INTERNAL_REFLECTION_MODE
- TOTAL_INTERNAL_REFLECTION_MODE
- FIBER_OPTIC_MODE
- WAVEGUIDE_MODE
- PHASED_ARRAY_MODE
- DOUBLE_SLIT_DIFFRACTION_MODE
- DIFFRACTION_GRATING_MODE
- MAZE_MODE

Pressing the right button resets the mode to its initial condition.
