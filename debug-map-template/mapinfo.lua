--------------------------------------------------------------------------------
-- mapinfo.lua for the SpringMapConvNG mipmap debug map.
-- The diffuse colours (red/green/blue/yellow per mip level) must not be altered
-- by the engine, so: detailTex is a neutral mid-grey (adds nothing under the
-- signed-additive detail blend), and ground ambient/diffuse lighting is white.
--------------------------------------------------------------------------------

local mapinfo = {
	name        = "Mipmap Debug Map",
	shortname   = "mipdebug",
	description = "Per-mip-level colours (red/green/blue/yellow) to visualise LOD selection",
	author      = "SpringMapConvNG",
	version     = "1.0",
	-- mapfile must point at the .smf inside the archive; match your -o name.
	mapfile     = "maps/debugmap.smf",
	modtype     = 3, --// 3 = map
	depend      = {"Map Helper v1"},

	maphardness     = 100,
	notDeformable   = false,
	gravity         = 130,
	tidalStrength   = 0,
	maxMetal        = 0.02,
	extractorRadius = 500.0,
	voidWater       = false,
	autoShowMetal   = true,

	smf = {
		minheight = 0,
		maxheight = 100,
	},

	resources = {
		-- Neutral detail texture: solid RGB(128,128,128). Under the shader's
		-- (detail*2 - 1) signed-additive blend this contributes ~zero, so the
		-- flat colours show without grain. Path is relative to the archive root;
		-- adjust if you place the file elsewhere.
		detailTex = "neutral_detail.png",
	},

	lighting = {
		sunStartAngle = 0.0,
		sunOrbitTime  = 1440.0,
		sunDir        = {0.0, 1.0, 2.0, 1e9},

		-- White ground lighting so (diffuse + detail) * shadeInt keeps the
		-- debug colours at full brightness on the flat terrain.
		groundAmbientColor  = {1.0, 1.0, 1.0},
		groundDiffuseColor  = {1.0, 1.0, 1.0},
		groundSpecularColor = {0.0, 0.0, 0.0},
		groundShadowDensity = 0.0,
	},
}

return mapinfo
