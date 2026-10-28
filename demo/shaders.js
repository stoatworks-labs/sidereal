// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
#define TAU_N 48
#define TAU_MAX 12.0
#define TEMP_N 64
#define LINE_N 10
#define STAR_TMIN 2500.0
#define STAR_TMAX 50000.0
#define DUST_TMIN 15.0
#define DUST_TMAX 600.0
#define MAX_ELEMENTS 64
#define MAX_SOURCES 12
#define MAX_GALAXIES 8
#define SN_N 512
#define SN_DAYS 800.0
#define C_OXYGEN 0
#define C_HYDROGEN 1
#define C_SULPHUR 2
#define C_SHOCK 3
#define C_MOLECULAR 4
#define C_METEOR_SLOW 5
#define C_METEOR_FAST 6
#define C_TRAIN 7
#define C_PAH 8
#define C_SYNCHROTRON 9
#define PI 3.14159265358979

#define HK_RAW_EMISSION 1
#define HK_RAW_FIELD 2
#define HK_RAW_LIGHT 4
#define HK_NO_STARS 8
#define HK_LINEAR 16
#define HK_NO_SHADOW 32
#define HK_ONE_ZONE 64
#define HK_GREY_PSF 128
#define HK_SPIKE_LAW 256
#define HK_LENS_BRIGHT 512
#define HK_NO_GAS 1024
#define HK_RAW_SPRITES 2048
#define HK_COARSE_MARCH 4096
#define HK_WRONG_PARALLAX 8192
#define HK_ECHO_SPHERE 16384
#define HK_LENS_MAP 32768

uniform int Hooks;
uniform mat2 Cam;
uniform vec2 CamPan;
uniform vec2 CamDrift;

// The scene point under a screen point, for something at this depth: the
// camera's turn and zoom, its pan, and its drift, which moves near things
// more than far ones.
vec2 sceneAt( vec2 s, float depth )
{
	float k = ( ( Hooks & HK_WRONG_PARALLAX ) != 0 ? depth : 1.0 / max( depth, 0.05 ) ) - 1.0;
	return Cam * s + CamPan + CamDrift * k;
}

float sq( float x )
{
	return x * x;
}

uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
uint hash2u( ivec2 c, uint salt )
{
	return pcg( uint( c.x ) ^ pcg( uint( c.y ) ^ pcg( salt + 0x9e3779b9u ) ) );
}
uint hash3u( ivec3 c )
{
	return pcg( uint( c.x ) ^ pcg( uint( c.y ) ^ pcg( uint( c.z ) + 0x7f4a7c15u ) ) );
}
float u01( uint h )
{
	return float( h >> 8u ) * ( 1.0 / 16777216.0 );
}

vec3 gradient3( ivec3 c )
{
	uint h = hash3u( c );
	return vec3( float( h & 1023u ), float( ( h >> 10u ) & 1023u ), float( ( h >> 20u ) & 1023u ) ) * ( 2.0 / 1023.0 ) - 1.0;
}

// Gradient noise, quintic fade, 0..1 about 0.5.
float gnoise3( vec3 p )
{
	vec3 i  = floor( p );
	vec3 f  = p - i;
	ivec3 c = ivec3( i );
	vec3 u  = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	float n000 = dot( gradient3( c ), f );
	float n100 = dot( gradient3( c + ivec3( 1, 0, 0 ) ), f - vec3( 1.0, 0.0, 0.0 ) );
	float n010 = dot( gradient3( c + ivec3( 0, 1, 0 ) ), f - vec3( 0.0, 1.0, 0.0 ) );
	float n110 = dot( gradient3( c + ivec3( 1, 1, 0 ) ), f - vec3( 1.0, 1.0, 0.0 ) );
	float n001 = dot( gradient3( c + ivec3( 0, 0, 1 ) ), f - vec3( 0.0, 0.0, 1.0 ) );
	float n101 = dot( gradient3( c + ivec3( 1, 0, 1 ) ), f - vec3( 1.0, 0.0, 1.0 ) );
	float n011 = dot( gradient3( c + ivec3( 0, 1, 1 ) ), f - vec3( 0.0, 1.0, 1.0 ) );
	float n111 = dot( gradient3( c + ivec3( 1, 1, 1 ) ), f - vec3( 1.0, 1.0, 1.0 ) );
	float n = mix( mix( mix( n000, n100, u.x ), mix( n010, n110, u.x ), u.y ), mix( mix( n001, n101, u.x ), mix( n011, n111, u.x ), u.y ), u.z );
	return clamp( 0.5 + 0.75 * n, 0.0, 1.0 );
}

float fbm3( vec3 p, int octaves )
{
	float sum = 0.0, amp = 0.5, norm = 0.0;
	for( int i = 0; i < octaves; ++i )
	{
		sum += amp * gnoise3( p );
		norm += amp;
		p   = p * 2.03 + vec3( 17.13, 9.31, 3.71 );
		amp *= 0.5;
	}
	return sum / norm;
}

// What a unit of a line component looks like through tau_V of dust.
vec3 lineLut( sampler2D lut, int c, float tau )
{
	float x = ( clamp( tau / TAU_MAX, 0.0, 1.0 ) * float( TAU_N - 1 ) + 0.5 ) / float( TAU_N );
	return texture( lut, vec2( x, ( float( c ) + 0.5 ) / float( LINE_N ) ) ).rgb;
}

float tempRow( int kind, float T )
{
	float lo = kind == 2 ? DUST_TMIN : STAR_TMIN;
	float hi = kind == 2 ? DUST_TMAX : STAR_TMAX;
	return clamp( log( max( T, lo ) / lo ) / log( hi / lo ), 0.0, 1.0 ) * float( TEMP_N - 1 );
}

// A continuum: 0 a star, 1 starlight scattered by dust, 2 dust's own glow.
vec3 contLut( sampler2D lut, int kind, float T, float tau )
{
	float x = ( clamp( tau / TAU_MAX, 0.0, 1.0 ) * float( TAU_N - 1 ) + 0.5 ) / float( TAU_N );
	float y = ( float( kind * TEMP_N ) + tempRow( kind, T ) + 0.5 ) / float( 3 * TEMP_N );
	return texture( lut, vec2( x, y ) ).rgb;
}
`;

// kFieldFragment, source/Shaders.cpp
export const FIELD_FRAGMENT = `
in vec2 uv;
layout( location = 0 ) out vec4 OutA;
layout( location = 1 ) out vec4 OutB;

uniform vec2 FieldExtent;// frame heights the buffer spans
uniform sampler2D Elements;
uniform sampler2D Poly;
uniform int ElementCount;
uniform float FlowTime;
uniform float Turb;
uniform float WarpScale;
uniform float HazeLevel;
uniform float HazeScale;
uniform float HazeMul;
uniform float GasScale;
uniform float IonK;
uniform sampler2D Clip;
uniform int ClipGas;
uniform vec4 ClipMap;// clip uv = s * xy + zw

float polygonDistance( vec2 p, int start, int count )
{
	vec2 v0  = texelFetch( Poly, ivec2( start + count - 1, 0 ), 0 ).xy;
	float d  = 1e9;
	float sg = 1.0;
	for( int i = 0; i < count; ++i )
	{
		vec2 v1 = texelFetch( Poly, ivec2( start + i, 0 ), 0 ).xy;
		vec2 e  = v1 - v0;
		vec2 w  = p - v0;
		vec2 b  = w - e * clamp( dot( w, e ) / max( dot( e, e ), 1e-12 ), 0.0, 1.0 );
		d       = min( d, dot( b, b ) );
		bvec3 c = bvec3( p.y >= v0.y, p.y < v1.y, e.x * w.y > e.y * w.x );
		if( all( c ) || all( not( c ) ) )
			sg = -sg;
		v0 = v1;
	}
	return sg * sqrt( d );
}

void main()
{
	vec2 s  = ( uv - 0.5 ) * FieldExtent;
	vec2 p1 = sceneAt( s, 1.0 );

	// The turbulence, shared: a warp that moves edges and a detail that
	// varies density, both evolving with the flow.
	vec2 warp  = vec2( fbm3( vec3( p1 * WarpScale, FlowTime * 0.5 ), 4 ), fbm3( vec3( p1 * WarpScale + 31.7, FlowTime * 0.5 + 7.1 ), 4 ) ) - 0.5;
	vec2 warp2 = vec2( fbm3( vec3( p1 * WarpScale * 4.3 + 11.1, FlowTime * 0.9 ), 3 ), fbm3( vec3( p1 * WarpScale * 4.3 + 57.3, FlowTime * 0.9 + 3.3 ), 3 ) ) - 0.5;
	warp = warp * 2.0 + warp2 * 1.1;
	float detailNoise = fbm3( vec3( p1 * WarpScale * 2.3 + 5.3, FlowTime * 0.7 ), 6 );
	float turb = min( Turb, 2.0 );

	vec4 A = vec4( 0.0 );
	vec4 B = vec4( 0.0 );
	// Traced shapes join where they meet, by the denser of the two: summed,
	// a pillar running on into the cloud it stands on was a ridge across its
	// feet, and trimmed to meet it, a groove.
	float polyCol = 0.0, polyIon = 0.0;
	for( int i = 0; i < ElementCount; ++i )
	{
		vec4 t0 = texelFetch( Elements, ivec2( i * 5 + 0, 0 ), 0 );// shape, channel, depth, polygon start
		vec4 t1 = texelFetch( Elements, ivec2( i * 5 + 1, 0 ), 0 );// x, y, x2, y2
		vec4 t2 = texelFetch( Elements, ivec2( i * 5 + 2, 0 ), 0 );// a, b, cos, sin
		vec4 t3 = texelFetch( Elements, ivec2( i * 5 + 3, 0 ), 0 );// density, thickness, edge, rough
		vec4 t4 = texelFetch( Elements, ivec2( i * 5 + 4, 0 ), 0 );// detail, extra, extra2, polygon count
		int shape = int( t0.x + 0.5 );
		int chan  = int( t0.y + 0.5 );
		// The in-plane opacity is the gas's density in the picture's own
		// plane: column over thickness for most shapes, the ring's slice for a
		// shell (its cavity is empty; its caps are only in front and behind).
		float slice = -1.0;
		vec2 p    = sceneAt( s, t0.z ) + warp * t3.w * Turb;
		vec2 d    = p - t1.xy;
		vec2 q    = vec2( dot( d, t2.zw ), dot( d, vec2( -t2.w, t2.z ) ) );
		float v   = 0.0;
		if( shape == 0 )
		{
			// Blob: an ellipse, a soft edge, a little fuller in the middle.
			float r  = length( q / t2.xy );
			float sd = ( r - 1.0 ) * min( t2.x, t2.y );
			v        = ( 1.0 - smoothstep( -t3.z, t3.z, sd ) ) * ( 0.55 + 0.45 * clamp( 1.0 - r * r, 0.0, 1.0 ) );
		}
		else if( shape == 1 )
		{
			// Column: a tapered capsule; its column density is a cylinder's chord.
			vec2 ba    = t1.zw - t1.xy;
			float h    = clamp( dot( d, ba ) / max( dot( ba, ba ), 1e-9 ), 0.0, 1.0 );
			float rad  = mix( t2.x, t2.y, h );
			float dist = length( d - ba * h );
			v = ( 1.0 - smoothstep( -t3.z, t3.z, dist - rad ) ) * sqrt( clamp( 1.0 - dist * dist / ( rad * rad + 1e-9 ), 0.05, 1.0 ) );
		}
		else if( shape == 2 )
		{
			// Polygon: traced from a picture. Deeper inside is a longer chord.
			float sd = polygonDistance( p, int( t0.w + 0.5 ), int( t4.w + 0.5 ) );
			v = ( 1.0 - smoothstep( -t3.z, t3.z, sd ) ) * clamp( 0.7 + 0.3 * ( -sd ) / ( 3.0 * t3.z + 1e-4 ), 0.7, 1.0 );
		}
		else if( shape == 3 )
		{
			// Shell: a uniform thick shell between radius 1 - fraction and 1,
			// projected: the difference of two chords, limb-brightened.
			vec2 n     = q / t2.xy;
			float r    = length( n );
			float ri   = 1.0 - t4.y;
			float edge = t3.z / min( t2.x, t2.y );
			float outer = sqrt( max( 1.0 - r * r, 0.0 ) );
			float inner = sqrt( max( ri * ri - r * r, 0.0 ) );
			float chord = max( sqrt( max( 1.0 - ri * ri, 1e-4 ) ), 1e-3 );
			// An open shell is a barrel seen down its axis, as the Ring is: the
			// gas over its cavity (a closed shell's caps) thins by its openness,
			// its wall does not. Closed, the caps filled every ring in.
			float caps = 1.0 - t1.z * ( 1.0 - smoothstep( ri - edge, ri + edge, r ) );
			v = ( outer - inner ) / chord * caps;
			v *= 1.0 - smoothstep( 1.0 - edge, 1.0 + edge, r );
			// The slice: density / ( 2 R chord ) between ri and 1, at a third of
			// a neutral wall's opacity. A planetary's shell is mostly ionised
			// right through (density-bounded): at the full opacity the ionised
			// skin was a texel or two of the wall, and the ring didn't show.
			slice = 0.35 * smoothstep( ri - edge, ri + edge, r ) * ( 1.0 - smoothstep( 1.0 - edge, 1.0 + edge, r ) ) / ( 2.0 * min( t2.x, t2.y ) * chord );
			if( t4.z > 0.0 )
			{
				// Knots: clumps strung out radially, on the shell's inner face.
				vec2 dir  = n / max( r, 1e-4 );
				float kn  = smoothstep( 0.55, 0.85, gnoise3( vec3( dir * 38.0, r * 3.0 + 11.0 ) ) );
				v *= mix( 1.0, 0.35 + 2.4 * kn, t4.z * smoothstep( ri - 0.1, ri + 0.15, r ) );
			}
		}
		else if( shape == 4 )
		{
			// Filaments: thin sheets seen edge on, a ridged network inside an
			// ellipse, brighter towards its rim, stretched along its angle.
			float r  = length( q / t2.xy );
			float m  = 1.0 - smoothstep( 0.72, 1.05, r );
			vec2 g   = vec2( q.x / ( 1.0 + 3.0 * t4.z ), q.y ) * t4.y;
			float n1 = gnoise3( vec3( g, FlowTime * 0.3 + 3.0 ) );
			float n2 = gnoise3( vec3( g * 2.1 + 7.7, FlowTime * 0.3 ) );
			float ridge = pow( 1.0 - abs( 2.0 * n1 - 1.0 ), 8.0 ) + 0.6 * pow( 1.0 - abs( 2.0 * n2 - 1.0 ), 10.0 );
			v = m * ridge * ( 0.35 + 0.65 * smoothstep( 0.15, 0.95, r ) );
		}
		else
		{
			// Sheet: denser along its direction, saturating over its edge width.
			v = clamp( dot( d, t1.zw ) / max( t3.z, 1e-3 ), 0.0, 1.0 );
		}
		if( t4.z > 0.0 && ( shape == 0 || shape == 5 ) )
		{
			// Streaks along the element's direction (the flow off an
			// ionisation front, a reflection nebula's combed dust).
			vec2 dir = shape == 5 ? t1.zw : t2.zw;
			vec2 a   = vec2( dot( p, dir ) / ( 1.0 + t4.z ), dot( p, vec2( -dir.y, dir.x ) ) ) * WarpScale * 2.2;
			float st = fbm3( vec3( a, FlowTime * 0.4 + 19.0 ), 4 );
			v *= mix( 1.0, clamp( 2.4 * st - 0.2, 0.0, 2.2 ), t4.z / ( 1.0 + t4.z ) );
		}
		float detail = t3.x * max( 0.0, 1.0 + t4.x * turb * ( detailNoise - 0.5 ) * 4.0 );
		v *= detail;

		if( chan == 0 && shape == 2 )
		{
			v *= GasScale;
			polyCol = max( polyCol, v );
			polyIon = max( polyIon, IonK * v / max( t3.y, 1e-3 ) );
		}
		else if( chan == 0 )
		{
			v *= GasScale;
			A.x += v;
			A.y += slice >= 0.0 ? IonK * slice * detail * GasScale : IonK * v / max( t3.y, 1e-3 );
		}
		else if( chan == 1 )
		{
			// The haze's opacity is its column's: the light pass reads it apart.
			v *= HazeMul;
			A.z += v;
		}
		else if( chan == 2 )
			A.w += v * GasScale;
		else if( chan == 3 )
			B.x += v;
		else if( chan == 4 )
			B.w += v;
		else if( chan == 5 )
			B.y += v;
		else
			B.z += v;
	}

	A.x += polyCol;
	A.y += polyIon;

	// The ionised interior, everywhere, patchy.
	float haze = HazeLevel * HazeMul * max( 0.0, 1.0 + 2.8 * ( fbm3( vec3( p1 * HazeScale, FlowTime * 0.3 + 2.0 ), 4 ) - 0.5 ) );
	A.z += haze;

	// The Over's clip as gas: its brightness is the column.
	if( ClipGas != 0 )
	{
		vec3 c     = texture( Clip, clamp( s * ClipMap.xy + ClipMap.zw, vec2( 0.0 ), vec2( 1.0 ) ) ).rgb;
		float luma = dot( c, vec3( 0.2126, 0.7152, 0.0722 ) ) * 1.2 * GasScale;
		A.x += luma;
		A.y += IonK * luma / 0.1;
	}
	OutA = A;
	OutB = B;
}
`;

// kLightFragment, source/Shaders.cpp
export const LIGHT_FRAGMENT = `
in vec2 uv;
layout( location = 0 ) out vec4 Out0;
layout( location = 1 ) out vec4 Out1;
layout( location = 2 ) out vec4 Out2;
layout( location = 3 ) out vec4 Out3;

uniform sampler2D FieldA;
uniform float Relief;    // a column's height per unit column density: its faces' slope
uniform float FaceShadow;// how much of the in-plane path a face's light still crosses
uniform vec2 FieldExtent;
uniform vec2 FieldTexels;
uniform int SourceCount;
uniform vec4 SrcA[ MAX_SOURCES ];// field u, field v, height, ionising luminosity
uniform vec4 SrcB[ MAX_SOURCES ];// oxygen hardness, visible luminosity, temperature, softening
uniform float HazeOpacity;// the ionised interior's in-plane opacity per unit column
uniform float HazeShade;  // how much of the haze behind lies in the dense gas's shadow
uniform float KO;    // [O III]'s photons, absorbed this much faster than hydrogen's
uniform float KS;    // the partly ionised zone's far-UV, this much slower
uniform float KP;    // far-UV through dust alone
uniform float KV;    // visible light through dust alone
uniform float PhaseG;// Henyey-Greenstein's asymmetry

void main()
{
	vec4 a = vec4( 0.0 );
	vec4 b = vec4( 0.0 );
	vec4 c = vec4( 0.0 );
	vec4 e = vec4( 0.0 );
	// The dense gas's faces towards us: its column as a height field, whose
	// slope turns each face towards or away from each star.
	vec2 tx   = 2.0 / FieldTexels;
	float gx  = ( textureLod( FieldA, uv + vec2( tx.x, 0.0 ), 1.0 ).x - textureLod( FieldA, uv - vec2( tx.x, 0.0 ), 1.0 ).x ) / ( 2.0 * tx.x * FieldExtent.x );
	float gy  = ( textureLod( FieldA, uv + vec2( 0.0, tx.y ), 1.0 ).x - textureLod( FieldA, uv - vec2( 0.0, tx.y ), 1.0 ).x ) / ( 2.0 * tx.y * FieldExtent.y );
	vec3 n    = normalize( vec3( -Relief * gx, -Relief * gy, 1.0 ) );
	float surface = 1.0 - exp( -3.0 * textureLod( FieldA, uv, 0.0 ).x );
	for( int k = 0; k < SourceCount; ++k )
	{
		vec2 dv  = SrcA[ k ].xy - uv;
		float r  = length( dv * FieldExtent );
		float hh = SrcA[ k ].z;
		float d3 = sqrt( r * r + hh * hh ) + 1e-4;

		// The part of the path inside the buffer: past its edge there is no gas.
		float tmax = 1.0;
		if( dv.x > 0.0 )
			tmax = min( tmax, ( 1.0 - uv.x ) / dv.x );
		else if( dv.x < 0.0 )
			tmax = min( tmax, -uv.x / dv.x );
		if( dv.y > 0.0 )
			tmax = min( tmax, ( 1.0 - uv.y ) / dv.y );
		else if( dv.y < 0.0 )
			tmax = min( tmax, -uv.y / dv.y );
		tmax = max( tmax, 0.0 );

		// The march: steps quadratically spaced from the pixel, so the front
		// right beside it is resolved and the far gas is read from the mip
		// level each step spans. The dense gas's opacity and the haze's are
		// summed apart: the haze fills the line of sight in front of and
		// behind the dense gas, and its light goes round a pillar in depth
		// where a flat picture would have it go through.
		const int N   = 40;
		float tauD    = 0.0;
		float tauH    = 0.0;
		float texels  = length( dv * FieldTexels );
		bool coarse   = ( Hooks & HK_COARSE_MARCH ) != 0;
		for( int i = 0; i < N; ++i )
		{
			float a0 = float( i ) / float( N ), a1 = float( i + 1 ) / float( N );
			float t0 = coarse ? a0 : a0 * a0;
			float t1 = coarse ? a1 : a1 * a1;
			float dt = ( t1 - t0 ) * tmax;
			float tm = 0.5 * ( t0 + t1 ) * tmax;
			float lod = log2( max( dt * texels, 1.0 ) );
			vec4 f = textureLod( FieldA, uv + dv * tm, lod );
			tauD += f.y * dt;
			tauH += f.z * dt;
		}
		// dt is a fraction of the in-plane path r; the path to a star off the
		// plane is longer by d3 / r.
		tauD *= d3;
		tauH *= d3 * HazeOpacity;
		if( ( Hooks & HK_NO_SHADOW ) != 0 )
		{
			tauD = 0.0;
			tauH = 0.0;
		}
		float tau = tauD + tauH;

		// The flux falls with the distance in depth too: gas seen near the
		// star in the picture can be far in front of or behind it. A shell's
		// gas is all at least its inner radius from its star: its caps at that
		// radius however close they look, its ring at the radius it is drawn.
		// (Adding the two in quadrature put the ring up to half as far again
		// as it is, and a third as bright, and filled every ring in.)
		float df = max( d3 * d3, SrcB[ k ].w * SrcB[ k ].w );
		float F  = SrcA[ k ].w / df;
		float Fv = SrcB[ k ].y / df;
		float ko = ( Hooks & HK_ONE_ZONE ) != 0 ? 1.0 : KO;
		float ks = ( Hooks & HK_ONE_ZONE ) != 0 ? 1.0 : KS;
		float hard = SrcB[ k ].x;
		// The dense gas: lit through everything between it and the star.
		a.x += F * exp( -tau );
		a.y += F * hard * exp( -ko * tau );
		a.z += F * max( exp( -ks * tau ) - exp( -tau ), 0.0 );
		a.w += Fv * exp( -KP * tau );
		// The haze in front of the dense gas sees the star round it (in depth);
		// the haze behind is in its shadow.
		b.x += F * exp( -tauH );
		b.y += F * hard * exp( -ko * tauH );
		float tauB = tauH + HazeShade * tauD;
		b.z += F * exp( -tauB );
		b.w += F * hard * exp( -ko * tauB );

		// Scattering towards the viewer, who is far away along +z: the light
		// arrives along ( r, -h ) / d3, so cos theta = -h / d3.
		float cosT  = -hh / d3;
		float g     = PhaseG;
		float phase = ( 1.0 - g * g ) / pow( 1.0 + g * g - 2.0 * g * cosT, 1.5 );
		float vis   = Fv * exp( -KV * tau ) * phase;
		c.x += Fv * max( exp( -KP * tau ) - exp( -tau ), 0.0 );
		c.y += Fv * exp( -KP * tauH );
		c.z += vis;
		c.w += vis * SrcB[ k ].z;

		// A face is an ionisation front seen face on: its brightness is the
		// flux normal to it. Its light crosses less of the in-plane path the
		// higher the star stands off the plane.
		vec3 L      = vec3( dv * FieldExtent, hh ) / d3;
		float cosI  = max( dot( n, L ), 0.0 ) * surface;
		// Inside a shell's cavity the gas is its caps, which its column already
		// counts; their slope read as a front facing the star lit every
		// planetary's middle flat.
		if( SrcB[ k ].w > 0.0 )
			cosI *= smoothstep( 0.85, 1.05, d3 / SrcB[ k ].w );
		// What stands in a face's way is bounded: past a few optical depths
		// of in-plane path the ray has long since risen clear of the gas.
		float faceT = FaceShadow * 6.0 * ( 1.0 - exp( -tauD / 6.0 ) ) * exp( -4.0 * hh / max( r, 1e-3 ) ) + tauH;
		e.x += F * cosI * exp( -faceT );
		e.y += Fv * cosI * exp( -KP * faceT );
		e.z += Fv * cosI * exp( -KV * faceT ) * phase;
		e.w += F * hard * cosI * exp( -ko * faceT );
	}
	Out0 = a;
	Out1 = b;
	Out2 = c;
	Out3 = e;
}
`;

// kSpriteVertex, source/Shaders.cpp
export const SPRITE_VERTEX = `
uniform sampler2D SpriteData;
uniform sampler2D FieldA;
uniform vec2 Raster;
uniform vec4 PxToField;// field uv = px * xy + zw
uniform float DustK;

flat out vec4 S0;
flat out vec4 S1;
flat out vec4 S2;
flat out vec4 S3;
out vec2 Px;

const vec2 kCorner[ 6 ] = vec2[ 6 ]( vec2( -1.0, -1.0 ), vec2( 1.0, -1.0 ), vec2( 1.0, 1.0 ), vec2( -1.0, -1.0 ), vec2( 1.0, 1.0 ), vec2( -1.0, 1.0 ) );

void main()
{
	int id = gl_VertexID / 6;
	vec2 c = kCorner[ gl_VertexID - id * 6 ];
	S0     = texelFetch( SpriteData, ivec2( id * 4 + 0, 0 ), 0 );
	S1     = texelFetch( SpriteData, ivec2( id * 4 + 1, 0 ), 0 );
	S2     = texelFetch( SpriteData, ivec2( id * 4 + 2, 0 ), 0 );
	S3     = texelFetch( SpriteData, ivec2( id * 4 + 3, 0 ), 0 );
	if( S1.x < 0.5 )
	{
		// A star: its dust is what lies in front of it, at its own place.
		vec4 f      = textureLod( FieldA, S0.xy * PxToField.xy + PxToField.zw, 0.0 );
		float layer = S1.z;
		S1.y = layer < 0.5 ? 0.0 : ( layer < 1.5 ? DustK * ( f.x + f.w ) : DustK * ( 0.6 * f.x + f.w ) );
		Px   = S0.xy + c * S1.w;
	}
	else
	{
		// A meteor: a strip along its trail, as far as its head has got.
		vec2 dir    = S0.zw;
		vec2 perp   = vec2( -dir.y, dir.x );
		float w     = S1.w;
		float len   = S1.y * min( S1.z, 1.0 );
		float along = mix( -w, len + w, c.x * 0.5 + 0.5 );
		Px = S0.xy + dir * along + perp * c.y * w;
	}
	gl_Position = vec4( Px / Raster * 2.0 - 1.0, 0.0, 1.0 );
}
`;

// kSpriteFragment, source/Shaders.cpp
export const SPRITE_FRAGMENT = `
flat in vec4 S0;
flat in vec4 S1;
flat in vec4 S2;
flat in vec4 S3;
in vec2 Px;
out vec4 Out;

uniform int FilterCount;
uniform vec3 FilterColour[ 8 ];
uniform float FilterExt[ 8 ];
uniform float FilterScale[ 8 ];// pivot / the telescope's reference wavelength
uniform sampler2D StarLut;     // TEMP_N x 8: each filter's gain x response to a blackbody
uniform sampler2D LineLut;
uniform float CoreSigma;       // px, at the reference wavelength
uniform float SeeingSigma;     // px
uniform float HaloFrac;
uniform float HaloWidth;       // Moffat alpha, in core sigmas
uniform int SpikeCount;
uniform vec4 SpikeDir[ 8 ];    // cos, sin, weight
uniform float SpikeAmp;
uniform float SpikeLen;        // px, at the reference wavelength
uniform float WakeGain;
uniform float MeteorGain;

float meteorLight( float x )
{
	if( x <= 0.0 || x >= 3.0 )
		return 0.0;
	float r = 1.0 - x / 3.0;
	return 2.25 * x * r * r;
}

void main()
{
	if( S1.x < 0.5 )
	{
		vec2 d     = Px - S0.xy;
		float r2   = dot( d, d );
		float trow = ( tempRow( 0, S0.w ) + 0.5 ) / float( TEMP_N );
		vec3 col   = vec3( 0.0 );
		for( int f = 0; f < FilterCount; ++f )
		{
			float resp = texture( StarLut, vec2( trow, ( float( f ) + 0.5 ) / 8.0 ) ).r;
			float sig0 = S0.z * resp * exp( -S1.y * FilterExt[ f ] );
			// Diffraction: the core and the spikes grow with wavelength.
			float sc   = ( Hooks & HK_GREY_PSF ) != 0 ? 1.0 : FilterScale[ f ];
			float sig  = sqrt( sq( CoreSigma * sc ) + sq( SeeingSigma ) );
			float sig2 = sig * sig;
			float core = ( 1.0 - HaloFrac ) * exp( -0.5 * r2 / sig2 ) / ( 2.0 * PI * sig2 );
			float hw   = HaloWidth * sig;
			float halo = HaloFrac * ( 1.5 / ( PI * hw * hw ) ) * pow( 1.0 + r2 / ( hw * hw ), -2.5 );
			float spikes = 0.0;
			for( int k = 0; k < SpikeCount; ++k )
			{
				float a = dot( d, SpikeDir[ k ].xy );
				if( a <= 0.0 )
					continue;
				float pp   = dot( d, vec2( -SpikeDir[ k ].y, SpikeDir[ k ].x ) );
				float r0   = 2.0 * sig;
				float fall = ( Hooks & HK_SPIKE_LAW ) != 0 ? r0 / ( r0 + a ) : r0 * r0 / ( r0 * r0 + a * a );
				spikes += SpikeDir[ k ].z * fall * exp( -a / ( SpikeLen * sc ) ) * exp( -0.5 * pp * pp / sig2 ) / ( 2.5066283 * sig );
			}
			col += FilterColour[ f ] * sig0 * ( core + halo + SpikeAmp * spikes );
		}
		Out = vec4( col, 1.0 );
		return;
	}

	// A meteor. S0 ( start px, direction ); S1 ( 1, length px, elapsed /
	// duration, half width ); S2 ( ln( 3 / x0 ), x0, peak, fast );
	// S3 ( train share, train life / duration, wake life / duration, sigma px ).
	vec2 d      = Px - S0.xy;
	float along = dot( d, S0.zw );
	float perp  = dot( d, vec2( -S0.w, S0.z ) );
	float L     = S1.y;
	float now   = S1.z;
	float head  = min( now, 1.0 );
	float frac  = clamp( along / L, 0.0, head );
	float sig   = S3.w;
	// Where the head was when it passed here, and how long ago.
	float light = meteorLight( S2.y * exp( S2.x * frac ) );
	float age   = now - frac;
	float prof  = exp( -0.5 * perp * perp / ( sig * sig ) ) / ( 2.5066283 * sig );
	float ends  = exp( -0.5 * sq( min( along, 0.0 ) ) / ( sig * sig ) ) * exp( -0.5 * sq( max( along - head * L, 0.0 ) ) / ( sig * sig ) );
	vec3 cHead  = mix( lineLut( LineLut, C_METEOR_SLOW, 0.0 ), lineLut( LineLut, C_METEOR_FAST, 0.0 ), S2.w );
	vec3 cTrain = lineLut( LineLut, C_TRAIN, 0.0 );
	float wake  = light * exp( -age / S3.z ) * WakeGain;
	float train = light * S3.x * exp( -age / S3.y );
	// The head itself, a point at the front while it still burns.
	float headLight = now < 1.0 ? meteorLight( S2.y * exp( S2.x * head ) ) : 0.0;
	vec2 hd         = d - S0.zw * head * L;
	float headPsf   = exp( -0.5 * dot( hd, hd ) / ( sig * sig ) ) / ( 2.0 * PI * sig * sig );
	vec3 col        = S2.z * MeteorGain * ( cHead * ( wake * prof * ends + headLight * headPsf * 4.0 ) + cTrain * train * prof * ends );
	Out = vec4( col, 1.0 );
}
`;

// kCompositeSky, source/Shaders.cpp
export const COMPOSITE_SKY = `
uniform sampler2D LineLut;
uniform sampler2D ContLut;
uniform sampler2D GalLut;
uniform sampler2D SnLut;
uniform float DustK;
uniform float StarDensity;
uniform float EmbeddedDensity;
uniform float StarFluxMin;
uniform float StarFluxMax;
uniform float StarSigma;  // px
uniform float SceneToPx;  // px per scene unit at depth 1
uniform float ScreenToPx; // px per frame height on the screen
uniform uint StarSalt;
uniform float FlySpeed;
uniform float FlyPhase;
uniform vec4 Band;        // amount, cos, sin, half width
uniform float BandOffset;
uniform vec4 SnA;         // x, y, days, active
uniform vec4 SnB;         // remnant radius, glow, echo distance, echo strength
uniform vec4 SnC;         // light per day, peak flux, temperature, 0
uniform float RemnantGain;
uniform float EchoGain;
uniform float BandGain;
uniform float ScatterGain;

float bandProfile( vec2 p )
{
	if( Band.x <= 0.0 )
		return 0.0;
	float d = dot( p, vec2( -Band.z, Band.y ) ) - BandOffset;
	return exp( -0.5 * d * d / ( Band.w * Band.w ) );
}

// A star's temperature from its hash: what a deep field of stars holds,
// mostly K and M dwarfs and giants, a few hot ones.
float starTemperature( uint h )
{
	float u = u01( h );
	if( u < 0.5 )
		return mix( 3000.0, 4900.0, u / 0.5 );
	if( u < 0.82 )
		return mix( 5000.0, 7200.0, ( u - 0.5 ) / 0.32 );
	if( u < 0.96 )
		return mix( 7500.0, 11000.0, ( u - 0.82 ) / 0.14 );
	return mix( 12000.0, 30000.0, ( u - 0.96 ) / 0.04 );
}

// One layer of faint stars: a hashed grid, at most one star a cell, each a
// Gaussian of the telescope's mean core. \`streak\` lengthens it radially
// (flying forward).
vec3 starLayer( vec2 p, vec2 s, float density, uint salt, float tau, float pxPerUnit, float occupancy, float brighten, float streak )
{
	if( density <= 0.0 )
		return vec3( 0.0 );
	float cell = sqrt( 0.5 / density );
	vec2 base  = floor( p / cell );
	vec3 col   = vec3( 0.0 );
	float sig  = StarSigma;
	vec2 radial = length( s ) > 1e-5 ? normalize( s ) : vec2( 1.0, 0.0 );
	for( int j = -1; j <= 1; ++j )
		for( int i = -1; i <= 1; ++i )
		{
			ivec2 c = ivec2( base ) + ivec2( i, j );
			uint h  = hash2u( c, salt );
			if( u01( h ) > 0.5 * occupancy )
				continue;
			uint h2  = pcg( h ^ 0x68e31da4u );
			uint h3  = pcg( h2 );
			uint h4  = pcg( h3 );
			vec2 pos = ( vec2( c ) + vec2( u01( h2 ), u01( h3 ) ) ) * cell;
			vec2 dp  = ( p - pos ) * pxPerUnit;
			float ar = dot( dp, radial ), at = dot( dp, vec2( -radial.y, radial.x ) );
			float sr = sqrt( sig * sig + streak * streak );
			float e  = 0.5 * ( ar * ar / ( sr * sr ) + at * at / ( sig * sig ) );
			if( e > 12.0 )
				continue;
			float flux = min( StarFluxMin * pow( max( u01( h4 ), 1e-4 ), -0.8 ), StarFluxMax ) * brighten;
			col += contLut( ContLut, 0, starTemperature( pcg( h4 ) ), tau ) * flux * exp( -e ) / ( 2.0 * PI * sr * sig );
		}
	return col;
}

vec3 fieldStars( vec2 s, float sigN, float tauBack )
{
	vec3 col   = vec3( 0.0 );
	float boost = 1.0 + 3.0 * bandProfile( sceneAt( s, 3.0 ) );
	if( FlySpeed > 0.0 )
	{
		// Forward through the stars: four layers each coming closer, scaled
		// about the middle as 1 / depth, born far and faint, gone past us.
		for( int L = 0; L < 4; ++L )
		{
			float ph    = fract( FlyPhase + float( L ) * 0.25 );
			float depth = mix( 4.0, 0.12, ph );
			float fade  = smoothstep( 0.0, 0.2, ph ) * ( 1.0 - smoothstep( 0.8, 1.0, ph ) );
			vec2 p      = Cam * s * depth;
			float streak = length( s ) * ScreenToPx * FlySpeed * 3.88 / 60.0 / depth;
			col += starLayer( p, s, StarDensity * 0.5 * boost, StarSalt + uint( 101 * L ) + uint( floor( FlyPhase + float( L ) * 0.25 ) ) * 977u,
			                  0.0, ScreenToPx / depth, 1.0, fade * 0.6 / depth, streak );
		}
		return col;
	}
	// Three depths: in front of the gas, behind it, far behind.
	col += starLayer( sceneAt( s, 0.7 ), s, StarDensity * 0.25 * boost, StarSalt + 11u, 0.0, SceneToPx, 1.0, 1.0, 0.0 );
	col += starLayer( sceneAt( s, 1.6 ), s, StarDensity * 0.35 * boost, StarSalt + 23u, tauBack, SceneToPx, 1.0, 1.0, 0.0 );
	col += starLayer( sceneAt( s, 3.5 ), s, StarDensity * 0.40 * boost, StarSalt + 37u, tauBack, SceneToPx, 1.0, 0.7, 0.0 );
	// Young stars still inside the dense gas: only an infrared telescope sees them.
	if( EmbeddedDensity > 0.0 && sigN > 0.05 )
		col += starLayer( sceneAt( s, 1.0 ), s, EmbeddedDensity, StarSalt + 59u, DustK * 0.6 * sigN, SceneToPx, clamp( sigN, 0.0, 1.0 ), 1.0, 0.0 );
	return col;
}

// The Milky Way's unresolved stars, behind everything.
vec3 milkyWay( vec2 s, float tauBack )
{
	vec2 p   = sceneAt( s, 3.0 );
	float b  = bandProfile( p );
	if( b <= 0.0 )
		return vec3( 0.0 );
	float n  = fbm3( vec3( p * 5.0, 1.7 ), 5 );
	return contLut( ContLut, 0, 4600.0, tauBack ) * Band.x * b * ( 0.4 + 1.2 * n ) * BandGain;
}

// A supernova's remnant, a thin shell of shocked gas, and its light echo off
// any dust in a sheet in front of it: the light that reaches us scattered
// from radius rho left the star sqrt( rho^2 + z^2 ) - z light-days earlier.
vec3 supernova( vec2 s, float sigN, float sigH, float sigF, float tauFore )
{
	if( SnA.w < 0.5 )
		return vec3( 0.0 );
	vec2 p    = sceneAt( s, 1.0 ) - SnA.xy;
	float rho = length( p );
	vec3 col  = vec3( 0.0 );
	float R   = SnB.x;
	if( R > 1e-4 && SnB.y > 0.0 )
	{
		float r  = rho / R;
		float ri = 0.82;
		float c  = ( sqrt( max( 1.0 - r * r, 0.0 ) ) - sqrt( max( ri * ri - r * r, 0.0 ) ) ) / sqrt( 1.0 - ri * ri );
		c *= 0.4 + 1.2 * gnoise3( vec3( p / R * 7.0, 3.0 ) );
		col += ( lineLut( LineLut, C_SHOCK, tauFore ) + 0.6 * lineLut( LineLut, C_OXYGEN, tauFore ) ) * c * SnB.y * RemnantGain;
	}
	if( SnB.w > 0.0 )
	{
		float z     = SnB.z;
		float delay = ( ( Hooks & HK_ECHO_SPHERE ) != 0 ? rho : sqrt( rho * rho + z * z ) - z ) / SnC.x;
		float t     = SnA.z - delay;
		float light = t > 0.0 ? texture( SnLut, vec2( ( t / SN_DAYS * float( SN_N - 1 ) + 0.5 ) / float( SN_N ), 0.5 ) ).r : 0.0;
		float dust  = 1.0 - exp( -DustK * 0.6 * ( sigN + sigF + 0.3 * sigH ) );
		float geo   = z * z / ( rho * rho + z * z );
		col += contLut( ContLut, 1, SnC.z, tauFore ) * SnB.w * SnC.y * light * dust * geo * EchoGain;
	}
	return col;
}
`;

// kCompositeGalaxies, source/Shaders.cpp
export const COMPOSITE_GALAXIES = `
uniform int GalaxyCount;
uniform vec4 Gal[ MAX_GALAXIES * 6 ];
uniform int DeepOn;
uniform int LensOn;
uniform vec4 DeepA;// density, lens x, lens y, Einstein radius
uniform vec4 DeepB;// z of the lens, members, core radius, radians per frame height
uniform float DA[ 33 ];// angular diameter distance, Mpc, z = 0 .. 8 by 0.25
uniform float GalaxyGain;
uniform float DeepGain;

// A galaxy: a Sersic bulge, an exponential disk with logarithmic spiral arms,
// dust lanes on the arms' inner edges and H II regions strung along them.
vec3 galaxy( int i, vec2 s, float tauFg )
{
	vec4 A = Gal[ i * 6 + 0 ];// x, y, disk scale, kind
	vec4 B = Gal[ i * 6 + 1 ];// cos i, sin i, cos pa, sin pa
	vec4 C = Gal[ i * 6 + 2 ];// arms, tan pitch, arm contrast, phase
	vec4 D = Gal[ i * 6 + 3 ];// bulge amplitude, Re, n, b_n
	vec4 E = Gal[ i * 6 + 4 ];// projected bulge axis, dust, dust ring, H II
	vec4 F = Gal[ i * 6 + 5 ];// disk T, bulge T, arm T, disk amplitude
	vec2 p = sceneAt( s, 1.0 ) - A.xy;
	vec2 u = vec2( dot( p, B.zw ), dot( p, vec2( -B.w, B.z ) ) );
	float h = A.z;

	float rb = length( vec2( u.x, u.y / E.x ) );
	float Ib = D.x * exp( -D.w * ( pow( rb / D.y + 1e-6, 1.0 / D.z ) - 1.0 ) );

	float Id = 0.0, young = 0.0, hii = 0.0, tauD = 0.0;
	if( A.w < 0.5 || A.w > 1.5 )
	{
		// A thin disk seen at inclination i, given a little thickness: its
		// surface brightness grows as 1 / cos i, the path through it.
		float ci   = sqrt( B.x * B.x + 0.012 );
		vec2 dp    = vec2( u.x, u.y / ci );
		float R    = length( dp );
		Id         = F.w * exp( -R / h ) / ci;
		float arm  = 0.0, dustArm = 0.0, hiiArm = 0.0;
		if( A.w < 0.5 && R > 1e-5 )
		{
			float theta = atan( dp.y, dp.x );
			float phi   = C.x * ( theta - log( R / h ) / C.y ) - C.w;
			arm     = pow( 0.5 + 0.5 * cos( phi ), 3.0 );
			dustArm = pow( 0.5 + 0.5 * cos( phi + 0.55 ), 6.0 );
			hiiArm  = pow( 0.5 + 0.5 * cos( phi - 0.25 ), 8.0 );
		}
		float clump = fbm3( vec3( dp / h * 3.0, 4.0 + float( i ) ), 4 );
		young = Id * C.z * arm * 1.8 * smoothstep( 0.3 * h, 1.2 * h, R );
		Id   *= 1.0 - 0.5 * C.z;
		float hk = smoothstep( 0.62, 0.8, fbm3( vec3( dp / h * 9.0, 9.0 + float( i ) ), 3 ) );
		// H II regions live where the gas is, along the arms inside a few
		// scale lengths; past that the disk is old stars.
		hii  = E.w * Id * hiiArm * hk * 4.0 * smoothstep( 0.4 * h, 1.5 * h, R ) * ( 1.0 - smoothstep( 2.5 * h, 4.0 * h, R ) );
		if( E.z > 0.0 )
			tauD = E.y * 3.0 * exp( -sq( ( R - E.z ) / ( 0.16 * E.z ) ) );
		else
			tauD = E.y * exp( -R / ( 1.4 * h ) ) * ( 0.4 + 2.2 * dustArm );
		tauD *= ( 0.5 + clump ) / ci;
	}
	// The bulge's far half is behind the disk's dust on the near side. The
	// change is spread over the disk's own thickness in the picture: a hard
	// switch at the major axis was a one-row step along an edge-on galaxy.
	float halfT = 0.08 * h * sqrt( 1.0 - B.x * B.x ) + 1e-4;
	float front = mix( 0.15, 1.0, smoothstep( halfT, -halfT, u.y ) );
	float bulge = Ib * ( 1.0 - 0.5 * front * ( 1.0 - exp( -tauD ) ) );
	float tauMix = 0.4 * tauD;
	vec3 col = contLut( ContLut, 0, F.x, tauFg + tauMix ) * Id * exp( -0.3 * tauD )
	         + contLut( ContLut, 0, F.z, tauFg + tauMix ) * young * exp( -0.3 * tauD )
	         + contLut( ContLut, 0, F.y, tauFg + front * 0.5 * tauD ) * bulge
	         + ( lineLut( LineLut, C_HYDROGEN, tauFg + tauMix ) + 0.25 * lineLut( LineLut, C_OXYGEN, tauFg + tauMix ) ) * hii;
	return col * GalaxyGain;
}

float angularDistance( float z )
{
	float x = clamp( z / 0.25, 0.0, 31.999 );
	int i   = int( x );
	return mix( DA[ i ], DA[ i + 1 ], x - float( i ) );
}

vec3 galaxyLut( float T, float z )
{
	float x = ( clamp( z / 8.0, 0.0, 1.0 ) * 31.0 + 0.5 ) / 32.0;
	float y = ( tempRow( 0, T ) + 0.5 ) / float( TEMP_N );
	return texture( GalLut, vec2( x, y ) ).rgb;
}

// One hashed layer of distant galaxies, evaluated at the source-plane point
// beta: a lens moves where each pixel looks, and surface brightness is
// conserved, so arcs fall out of evaluating the unlensed sky at beta.
vec3 galaxyLayer( vec2 beta, float cell, float occupancy, uint salt, float zMin, float sizeKpc )
{
	vec2 base = floor( beta / cell );
	vec3 col  = vec3( 0.0 );
	for( int j = -1; j <= 1; ++j )
		for( int i = -1; i <= 1; ++i )
		{
			ivec2 c = ivec2( base ) + ivec2( i, j );
			uint h  = hash2u( c, salt );
			if( u01( h ) > occupancy )
				continue;
			uint h2 = pcg( h ^ 0x1b873593u ), h3 = pcg( h2 ), h4 = pcg( h3 ), h5 = pcg( h4 ), h6 = pcg( h5 );
			vec2 pos = ( vec2( c ) + vec2( u01( h2 ), u01( h3 ) ) ) * cell;
			float z  = max( zMin, 0.25 + 5.5 * pow( u01( h4 ), 1.7 ) );
			// Angular size: a physical size over the angular diameter
			// distance, which stops shrinking past z ~ 1.6.
			// The search reaches one cell from the pixel's own and no further,
			// so a galaxy is kept to a quarter of a cell and faded to nothing a
			// cell from its centre: one cut off where the search stops was a
			// square edge in the sky.
			float re = min( sizeKpc * ( 0.5 + u01( h5 ) ) / pow( 1.0 + z, 0.8 ) * 1e-3 / angularDistance( z ) / DeepB.w, 0.25 * cell );
			vec2 d   = beta - pos;
			float reach = smoothstep( cell, 0.5 * cell, length( d ) );
			if( reach <= 0.0 )
				continue;
			float ca = u01( h6 ) * 2.0 - 1.0;
			float sa = sqrt( 1.0 - ca * ca ) * ( ( h6 & 1u ) == 0u ? 1.0 : -1.0 );
			vec2 q   = vec2( dot( d, vec2( ca, sa ) ), dot( d, vec2( -sa, ca ) ) );
			float axis = 0.3 + 0.7 * u01( pcg( h6 ) );
			float r  = length( vec2( q.x, q.y / axis ) ) / max( re, 1e-6 );
			if( r > 8.0 )
				continue;
			float kind = u01( pcg( h5 ) );
			float sb, T;
			if( kind < 0.3 )
			{
				sb = exp( -7.67 * ( pow( r + 1e-4, 0.25 ) - 1.0 ) ) * 0.12;
				T  = 4300.0;
			}
			else if( kind < 0.8 )
			{
				float th = atan( q.y / axis, q.x );
				sb = exp( -1.68 * r ) * ( 0.7 + 0.6 * pow( 0.5 + 0.5 * cos( 2.0 * th - 3.0 * log( r + 0.2 ) ), 2.0 ) ) + 0.6 * exp( -12.0 * r );
				T  = 6500.0 + 3000.0 * u01( h3 ^ h5 );
			}
			else
			{
				// Clumpy young galaxies, common at high redshift.
				float k = gnoise3( vec3( q / re * 1.6, float( h & 255u ) ) );
				sb = exp( -1.2 * r ) * smoothstep( 0.45, 0.8, k ) * 2.0;
				T  = 14000.0;
			}
			// Brighter in the past: star formation was ~ ( 1 + z )^2.5 busier.
			float lum = pow( max( u01( h ^ 0x85ebca6bu ), 1e-3 ), -0.7 ) * pow( 1.0 + z, 2.5 );
			col += galaxyLut( T, z ) * sb * lum * reach;
		}
	return col;
}

vec3 deepField( vec2 s, float tauBack )
{
	if( DeepOn == 0 )
		return vec3( 0.0 );
	vec2 p    = sceneAt( s, 4.0 );
	vec2 beta = p;
	float boost = 1.0;
	float zMin  = 0.0;
	if( LensOn != 0 )
	{
		// A singular isothermal sphere: every ray bent by the Einstein radius
		// towards the cluster's centre.
		vec2 th = p - DeepA.yz;
		float r = length( th );
		beta    = p - DeepA.w * th / max( r, 1e-5 );
		zMin    = DeepB.x + 0.15;
		if( ( Hooks & HK_LENS_BRIGHT ) != 0 )
			boost = r / max( abs( r - DeepA.w ), 1e-3 );
		if( ( Hooks & HK_LENS_MAP ) != 0 )
			return vec3( length( beta - DeepA.yz ) );
	}
	float area = DeepA.x;
	vec3 col   = galaxyLayer( beta, 0.016, clamp( area * 0.7 * 0.016 * 0.016, 0.0, 0.9 ), 71u, zMin, 4.0 );
	col       += galaxyLayer( beta, 0.05, clamp( area * 0.3 * 0.05 * 0.05, 0.0, 0.9 ), 113u, zMin, 9.0 );
	col *= boost;
	if( LensOn != 0 && DeepB.y > 0.0 )
	{
		// The cluster's own galaxies, unlensed, concentrated to its core.
		float r    = length( p - DeepA.yz );
		float dens = DeepB.y / ( 1.0 + sq( r / DeepB.z ) );
		vec3 m     = galaxyLayer( p, 0.03, clamp( dens * 0.03 * 0.03, 0.0, 0.9 ), 151u, DeepB.x, 12.0 );
		col += m * 0.4;
	}
	return col * DeepGain;
}
`;

// kCompositeMain, source/Shaders.cpp
export const COMPOSITE_MAIN = `
in vec2 uv;
out vec4 Out;

uniform float Aspect;
uniform vec4 FieldMap;// field uv = s * xy + zw
uniform sampler2D FieldA;
uniform sampler2D FieldB;
uniform sampler2D Light0;
uniform sampler2D Light1;
uniform sampler2D Light2;
uniform sampler2D Light3;
uniform sampler2D Sprites;
uniform sampler2D Clip;
uniform float IonK;
uniform float HazeIon;
uniform float SelfFrac;
uniform float HazeDust;
uniform float HardRef;
uniform float FrontHaze;
uniform float FaceGain;
uniform float MolSkin;
uniform vec4 EmitA;// hydrogen, oxygen, sulphur, aromatics
uniform vec4 EmitB;// dust glow, scattering, shock, synchrotron
uniform vec4 EmitC;// jets, oxygen-rich shock, the interstellar field, the field that makes 90 K
uniform float Exposure;
uniform float StretchQ;
uniform float Saturation;
uniform int Effect;
uniform int ClipMode;
uniform float Mix;
uniform vec3 ClipExt;
uniform vec4 ClipMap;

vec3 stretch( vec3 c )
{
	c = max( c, 0.0 ) * Exposure;
	// Lupton et al. 2004: one asinh of the mean, applied to every channel,
	// so a star's colour survives its core's brightness.
	float I = ( c.r + c.g + c.b ) / 3.0;
	if( StretchQ > 0.0 && I > 1e-9 )
		c *= asinh( StretchQ * I ) / ( asinh( StretchQ ) * I );
	float L = dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
	c = max( mix( vec3( L ), c, Saturation ), 0.0 );
	return min( c, vec3( 1.0 ) );
}

void main()
{
	vec2 s   = ( uv - 0.5 ) * vec2( Aspect, 1.0 );
	vec2 fuv = s * FieldMap.xy + FieldMap.zw;
	vec4 A   = texture( FieldA, fuv );
	vec4 B   = texture( FieldB, fuv );
	vec4 L0  = texture( Light0, fuv );
	vec4 L1  = texture( Light1, fuv );
	vec4 L2  = texture( Light2, fuv );
	vec4 L3  = texture( Light3, fuv );
	if( ( Hooks & HK_NO_GAS ) != 0 )
	{
		A = vec4( 0.0 );
		B = vec4( 0.0 );
		L0 = vec4( 0.0 );
		L1 = vec4( 0.0 );
		L2 = vec4( 0.0 );
		L3 = vec4( 0.0 );
	}
	float sigN = A.x, sigH = A.z, sigF = A.w;
	float tauFore = DustK * sigF;
	float tauSelf = DustK * ( SelfFrac * sigN + sigF );
	float tauBack = DustK * ( sigN + sigF + HazeDust * sigH );

	// Every ionising photon absorbed makes a recombination: the gas's line
	// light is the light it absorbed, column x opacity x what arrived. The
	// dense gas and the haze are lit apart (the light pass says why), and the
	// haze is split: FrontHaze of it in front of the dense gas, the rest behind.
	float hazeAbs = IonK * HazeIon * sigH;
	float eH      = EmitA.x * IonK * sigN * L0.x;
	float eHf     = EmitA.x * hazeAbs * FrontHaze * L1.x;
	float eHb     = EmitA.x * hazeAbs * ( 1.0 - FrontHaze ) * L1.z;
	// [O III] is a forbidden line that carries much of the cooling where O++
	// lives, not one photon per ionisation: its zone is where the hard photons
	// reach, its strength normalised to a 40 kK star's (HardRef).
	float eO      = EmitA.y * IonK * sigN * L0.y / HardRef;
	float eOf     = EmitA.y * hazeAbs * FrontHaze * L1.y / HardRef;
	float eOb     = EmitA.y * hazeAbs * ( 1.0 - FrontHaze ) * L1.w / HardRef;
	float eS      = EmitA.z * IonK * sigN * L0.z;
	float ePah    = EmitA.w * IonK * sigN * L2.x;
	// Fluorescent H2 in the same skin, just beyond the front.
	float eMol    = MolSkin * IonK * sigN * L2.x;
	// The faces: fronts seen face on, H, a front's [S II], the aromatics,
	// and starlight scattered off them; only the foreground dims them.
	float eHface  = EmitA.x * FaceGain * L3.x;
	float eOface  = EmitA.y * FaceGain * L3.w / HardRef * 0.3;
	float eSface  = EmitA.z * FaceGain * L3.x * 1.5;
	// A face is a photodissociation region seen face on: its aromatics glow.
	float ePface  = EmitA.w * FaceGain * 15.0 * L3.y;
	// Dust radiates what it absorbs, at the temperature that takes.
	float field = L0.w + EmitC.z;
	float Td    = 90.0 * pow( field / EmitC.w, 1.0 / 6.0 );
	float eD    = EmitB.x * ( sigN * field + ( 0.3 * sigH + 0.3 * sigF ) * ( L2.y + EmitC.z ) );
	float Tsc   = L2.z > 1e-12 ? L2.w / L2.z : 6000.0;
	float eSc   = EmitB.y * ScatterGain * ( ( 1.0 - exp( -DustK * 0.6 * ( sigN + sigF ) ) ) * L2.z + FaceGain * 0.3 * L3.z );

	if( ( Hooks & HK_RAW_EMISSION ) != 0 )
	{
		Out = vec4( eO + eOf + eOb + eOface, eH + eHf + eHb + eHface, eS + eSface, ePah + ePface );
		return;
	}
	if( ( Hooks & HK_RAW_FIELD ) != 0 )
	{
		Out = A;
		return;
	}
	if( ( Hooks & HK_RAW_LIGHT ) != 0 )
	{
		Out = L0;
		return;
	}
	if( ( Hooks & HK_RAW_SPRITES ) != 0 )
	{
		Out = vec4( texture( Sprites, uv ).rgb, 1.0 );
		return;
	}

	vec3 col = lineLut( LineLut, C_HYDROGEN, tauSelf ) * eH + lineLut( LineLut, C_OXYGEN, tauSelf ) * eO
	         + lineLut( LineLut, C_HYDROGEN, tauFore ) * eHf + lineLut( LineLut, C_OXYGEN, tauFore ) * eOf
	         + lineLut( LineLut, C_HYDROGEN, tauBack ) * eHb + lineLut( LineLut, C_OXYGEN, tauBack ) * eOb
	         + lineLut( LineLut, C_SULPHUR, tauSelf ) * eS + lineLut( LineLut, C_PAH, tauSelf ) * ePah
	         + lineLut( LineLut, C_HYDROGEN, tauFore ) * eHface + lineLut( LineLut, C_OXYGEN, tauFore ) * eOface
	         + lineLut( LineLut, C_SULPHUR, tauFore ) * eSface + lineLut( LineLut, C_PAH, tauFore ) * ePface
	         + lineLut( LineLut, C_MOLECULAR, tauSelf ) * eMol
	         + contLut( ContLut, 2, Td, tauSelf ) * eD + contLut( ContLut, 1, Tsc, tauSelf ) * eSc;
	col += lineLut( LineLut, C_SHOCK, tauFore ) * EmitB.z * B.x + lineLut( LineLut, C_OXYGEN, tauFore ) * EmitC.y * B.w
	     + lineLut( LineLut, C_SYNCHROTRON, tauFore ) * EmitB.w * B.y + lineLut( LineLut, C_MOLECULAR, tauSelf ) * EmitC.x * B.z;

	for( int i = 0; i < GalaxyCount; ++i )
		col += galaxy( i, s, tauBack );
	col += deepField( s, tauBack );
	if( ( Hooks & HK_NO_STARS ) == 0 )
	{
		col += fieldStars( s, sigN, tauBack );
		col += milkyWay( s, tauBack );
		col += texture( Sprites, uv ).rgb;
	}
	col += supernova( s, sigN, sigH, sigF, tauFore );

	vec3 result = ( Hooks & HK_LINEAR ) != 0 ? col * Exposure : stretch( col );
	float alpha = 1.0;
	if( Effect != 0 )
	{
		vec4 clip = texture( Clip, s * ClipMap.xy + ClipMap.zw );
		if( ClipMode == 0 )
			result = min( clip.rgb * exp( -tauBack * ClipExt ) + result, vec3( 1.0 ) );
		result = mix( clip.rgb, result, Mix );
		alpha  = mix( clip.a, 1.0, Mix );
	}
	Out = vec4( result, alpha );
}
`;
