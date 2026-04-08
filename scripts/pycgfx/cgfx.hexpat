#include <std/array.pat>
#include <std/string.pat>
#include <std/ptr.pat>

// This pattern allows for inspecting and modifying CGFX files using ImHex.
// WARNING: Due to the large number of nested templates, evaluating this pattern uses
// several gigabytes of memory.

// https://github.com/PabloMK7/EveryFileExplorer/blob/43c08c11af58567f06a7118bb9af88ba74e810ba/3DS/NintendoWare/GFX/CMDL.cs#L763
// https://github.com/gdkchan/Ohana3DS-Rebirth/blob/master/Ohana3DS%20Rebirth/Ohana/Models/CGFX.cs

struct RelPtr<T> {
    u32 ptr [[no_unique_address, hidden]];
    if (ptr == 0) {
        padding[4];
    } else {
        T *data: s32 [[inline, pointer_base("std::ptr::relative_to_pointer")]];
    }
};

struct ListPtr<T> {
    u32 count;
    T *entries[count]: s32 [[inline, pointer_base("std::ptr::relative_to_pointer")]];
};

using Matrix = std::Array<std::Array<float, 4>, 3>;
using OrientationMatrix = std::Array<std::Array<float, 3>, 3>;
using NullString = std::string::NullString;

bitfield MTOBFlags {
    bool isFragmentLightEnabled: 1;
    bool iVertexLightEnabled: 1;
    bool isHemiSphereLighEnabled: 1;
    bool isHemiSphereOcclusionEnabled: 1;
    bool isFogEnabled: 1;
    bool iPolygonOffsetEnabled: 1;
    padding: 26;
};

struct ColorFloat {
    float r, g, b, a;
};
struct ColorByte {
    u8 r, g, b, a;
};

enum TextureFormat: u32 {
    RGBA8,
    RGB8,
    RGBA5551,
    RGB565,
    RGBA4,
    LA8,
    HILO8,
    L8,
    A8,
    LA4,
    L4,
    A4,
    ETC1,
    ETC1A4,
};

enum LoopMode: u32 {
    OneTime,
    Loop,
};

enum CullMode: u32 {
    Never,
    FrontFace,
    BackFace,
};

enum TevSrc: u8 {
    PrimaryColor,
    FragmentPrimaryColor,
    FragmentSecondaryColor,
    Texture0,
    Texture1,
    Texture2,
    Texture3,
    PreviousBuffer = 0xd,
    Constant = 0xe,
    Previous = 0xf,
};

bitfield TevSources {
    TevSrc src0: 4;
    TevSrc src1: 4;
    TevSrc src2: 4;
};

enum TevOpRGB: u8 {
    SrcColor = 0,
    OneMinusSrcColor = 1,
    SrcAlpha = 2,
    OneMinusSrcAlpha = 3,
    SrcR = 4,
    OneMinusSrcR = 5,
    SrcG = 8,
    OneMinusSrcG = 9,
    SrcB = 12,
    OneMinusSrcB = 13,
};

enum TevOpA: u8 {
    SrcA,
    OneMinusSrcA,
    SrcR,
    OneMinusSrcR,
    SrcG,
    OneMinusSrcG,
    SrcB,
    OneMinusSrcB,
};

bitfield TevOps {
    TevOpRGB rgb0: 4;
    TevOpRGB rgb1: 4;
    TevOpRGB rgb2: 4;
    TevOpA alpha0: 4;
    TevOpA alpha1: 4;
    TevOpA alpha2: 4;
    padding: 8;
};

enum TevCombine: u16 {
    Replace,
    Modulate,
    Add,
    AddSigned,
    Interpolate,
    Subtract,
    Dot3RGB,
    Dot3RGBA,
    MultiplyAdd,
    AddMultiply,
};

enum TevScale: u16 {
    x1,
    x2,
    x4,
};

enum TextureProjection: u32 {
    UVMap,
    CameraCubeMap,
    CameraSphereMap,
    ProjectionMap,
    ShadowMap,
    ShadowCubeMap,
};

enum FresnelConfig: u32 {
    None,
    Primary,
    Secondary,
    PrimarySecondary,
};

enum BumpMode: u32 {
    NotUsed,
    AsBump,
    AsTangent,
};

enum SkeletonScalingRule: u32 {
    Standard,
    Maya,
    SoftImage,
};

enum BillboardMode: u32 {
    Off,
    World,
    WorldViewpoint,
    Screen,
    ScreenViewpoint,
    YAxial,
    YAxialViewpoint,
};

enum DataType: u32 {
    Byte = 0x1400,
    UnsignedByte = 0x1401,
    Short = 0x1402,
    UnsignedShort = 0x1403,
    Float = 0x1406,
};

struct CGFXHeader {
    char magic[4];
    u16 endianness;
    u16 headerSize;
    u32 version;
    u32 fileSize;
    u32 nrBlocks;
};

struct DictNode<T> {
    u32 refBit;
    u16 leftIndex, rightIndex;
    RelPtr<NullString> name;
    RelPtr<T> data;
};

struct DICT<T> {
    char magic[4];
    u32 sectionSize;
    u32 nrEntries;
    DictNode<T> rootNode;
    DictNode<T> nodes[nrEntries] [[inline]];
};


struct DictInfo<T> {
    u32 nrItems;
    u32 offset [[hidden]];
    if (offset != 0) {
        DICT<T> dict @ $ + offset - 4;
    }
};

struct MTOBTextureCoordinator {
    u32 sourceCoordinate;
    TextureProjection projection;
    u32 referenceCamera;
    u32 matrixMode;
    float scaleU;
    float scaleV;
    float rotate;
    float translateU;
    float translateV;
    u32 enabled;
    Matrix transformMatrix;
};

struct SHDR {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    if (type == 0x80000001) {
        RelPtr<NullString> referenceShaderName;
        padding[4];
    } else if (type == 0x80000002) {
        RelPtr<NullString>;
        RelPtr<char> dvlb;
    }
} [[inline]];

bitfield PicaCommandHeader {
    cmd_id: 16;
    param_mask: 4;
    extra_data_length: 8;
    group_commands: 1;
};

struct PicaCommand {
    u32 param;
    PicaCommandHeader header;
    //u32 extra_values[header.extra_data_length];
};

struct TextureCombinerCtr {
    u32 constant;
    TevSources srcRgb;
    TevSources srcAlpha;
    PicaCommandHeader header;
    TevOps opr;
    TevCombine combineRgb;
    TevCombine combineAlpha;
    ColorByte constRgba;
    TevScale scaleRgb;
    TevScale scaleAlpha;
};

bitfield FragmentLightingFlags {
    bool ClampHighLight: 1;
    bool UseDistribution0: 1;
    bool UseDistribution1: 1;
    bool UseGeometricFactor0: 1;
    bool UseGeometricFactor1: 1;
    bool UseReflection: 1;
    padding: 26;
};

struct FragmentLightingCtr {
    FragmentLightingFlags flags;
    u32 layerConfig;
    FresnelConfig fresnelConfig;
    u32 bumpTexture;
    BumpMode bumpMode;
    u32 isBumpRenormalize;
};

struct LutTable {
    u32 type;
    RelPtr<NullString> name;
    u32 someBoolean;
    ListPtr<u8> lutCommands;
} [[inline]];

struct LUTS {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    DictInfo<LutTable> tables;
} [[inline]];

struct ReferenceLookupTableCtr {
    u32 type;
    RelPtr<NullString> binaryPath;
    RelPtr<NullString> tableName;
    RelPtr<LUTS> targetLut; // runtime
} [[inline]];

struct LightingLookupTableCtr {
    u32 inputCommand;
    u32 scaleCommand;
    RelPtr<ReferenceLookupTableCtr> sampler;
} [[inline]];

struct FragmentLightingTableCtr {
    RelPtr<LightingLookupTableCtr> reflectanceRSampler;
    RelPtr<LightingLookupTableCtr> reflectanceGSampler;
    RelPtr<LightingLookupTableCtr> reflectanceBSampler;
    RelPtr<LightingLookupTableCtr> distribution0Sampler;
    RelPtr<LightingLookupTableCtr> distribution1Sampler;
    RelPtr<LightingLookupTableCtr> fresnelSampler;
} [[inline]];

struct FragmentShader {
    ColorFloat bufferColor;
    FragmentLightingCtr fragmentLighting;
    RelPtr<char> fragmentLightingTable;
    TextureCombinerCtr textureCombiners[6];
    PicaCommand alphaTestCommand;
    PicaCommand bufferCommands[3];
} [[inline]];

struct ReferenceTexture {
    RelPtr<NullString> linkedName;
} [[inline]];

struct PixelBasedImage {
    u32 height;
    u32 width;
    ListPtr<u8> data;
    u32 dynamicAllocator;
    u32 bitsPerPixel;
    u32 locationAddress;
    u32 memoryAddress;
} [[inline]];

struct ImageTexture {
    u32 height;
    u32 width;
    u32 glFormat;
    u32 glType;
    u32 mipmapLevels;
    u32 textureObject;
    u32 locationFlags;
    TextureFormat texFormat;
    RelPtr<PixelBasedImage> image;
} [[inline]];

struct TXOB {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    if (type == 0x20000004) {
        ReferenceTexture referenceTexture;
    } else if (type == 0x20000011) {
        ImageTexture imageTexture;
    }
} [[inline]];

struct Sampler {
    u32 type;
    char *ownerOffset: s32 [[pointer_base("std::ptr::relative_to_pointer")]];
    u32 minFilter;
    ColorFloat borderColor;
} [[inline]];

struct TexInfo {
    u32 type;
    u32 dynamicAllocator;
    RelPtr<TXOB> txobOffset;
    RelPtr<Sampler> samplerOffset;
    PicaCommand cmd[2];
    u16 height, width;
    padding[9*4];
    u32 commandSizeToSend;
} [[inline]];

struct MaterialColorCtr {
    ColorFloat emissionF;
    ColorFloat ambientF;
    ColorFloat diffuseF;
    ColorFloat specularF[2];
    ColorFloat constantF[6];
    ColorByte emissionB;
    ColorByte ambientB;
    ColorByte diffuseB;
    ColorByte specularB[2];
    ColorByte constantB[6];
    u32 commandCache;
};

struct RasterizationCtr {
    u32 isPolygonOffsetEnabled;
    CullMode cullMode;
    float polygonOffsetUnit;
    PicaCommand rasterizationCommand;
};

struct DepthOperationCtr {
    u32 depthFlags;
    PicaCommand depthCommands[2];
};

struct BlendOperationCtr {
    u32 blendMode;
    ColorFloat blendColor;
    PicaCommand blendCommands[3];
};

struct FragmentOperationCtr {
    DepthOperationCtr depthOperation;
    BlendOperationCtr blendOperation;
    PicaCommand stencilCommands[2];
};

struct MTOB {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    MTOBFlags flags;
    u32 textureCoordinatesConfig;
    u32 transluscencyKind;
    MaterialColorCtr materialColor;
    RasterizationCtr rasterization;
    FragmentOperationCtr fragmentOperations;
    u32 usedTexureCoordinates;
    MTOBTextureCoordinator textureCoordinators[3];
    RelPtr<TexInfo> texMappers[4];
    RelPtr<SHDR> shader;
    RelPtr<FragmentShader> fragmentShader;
    u32 shaderProgramDescriptionIndex;
    u32 shaderParametersCount;
    RelPtr<char> shaderParametersPointerTable;
    u32 lightSetIndex;
    u32 fogIndex;

    u32 shadingParametersHash, shaderParametersHash,
    textureCoordinatorsHash, textureSamplersHash,
    textureMappersHash, materialColorHash,
    rasterizationHash, fragmentLightingHash,
    fragmentLightingTableHash, fragmentLightingTableParametersHash,
    textureCombinersHash, alphaTestHash,
    fragmentOperationsHash;
    u32 materialId;
} [[inline]];

enum AnimationGroupMemberType: u32 {
    MeshNodeVisibility = 0x00080000,
    Mesh = 0x01000000,
    TextureSampler = 0x02000000,
    BlendOperation = 0x04000000,
    MaterialColor = 0x08000000,
    Model = 0x10000000,
    TextureMapper = 0x20000000,
    Bone = 0x40000000,
    TextureCoordinator = 0x80000000
};

enum AGMFieldType: u32 {
    MaterialColor = 1,
    Sampler = 2,
    TextureMapper = 3,
    BlendOperation = 4,
    TextureCoordinator = 5,
};

struct AnimationGroupMember {
    AnimationGroupMemberType objectType;
    RelPtr<NullString> path;
    RelPtr<NullString> member;
    RelPtr<NullString> objectId;
    u32 valueOffset;
    u32 valueSize;
    u32 unknown;
    AGMFieldType fieldType;
    u32 valueIndex;
    padding[4];
    if (fieldType <= 5) {
        RelPtr<NullString> parentName;
        u32 fieldIndex;
    } else {
        u32 parentIndex;
    }
} [[inline]];

bitfield GraphicsAnimGroupFlags {
    bool IsTransform: 1;
    padding: 31;
};

enum GraphicsMemberType: u32 {
    None,
    Bone,
    Material,
    Model,
    Light,
    Camera,
    Fog,
};

enum AnimGroupEvaluationTiming: u32 {
    BeforeWorldUpdate,
    AfterSceneCulling,
};

struct GraphicsAnimationGroup {
    u32 type;
    GraphicsAnimGroupFlags flags;
    RelPtr<NullString> name;
    GraphicsMemberType memberType;
    DictInfo<AnimationGroupMember> members;
    ListPtr<u32> blendOperations;
    AnimGroupEvaluationTiming evaluationTiming;
} [[inline]];

bitfield BoneFlags {
    bool isIdentity: 1;
    bool isTranslateZero: 1;
    bool isRotateZero: 1;
    bool isScaleOne: 1;
    bool isUniformScale: 1;
    bool isSegmentScaleCompensate: 1;
    bool isNeedRendering: 1;
    bool isLocalMatrixCalculate: 1;
    bool isWorldMatrixCalculate: 1;
    bool hasSkinningMatrix: 1;
    padding: 22;
};

struct Bone {
    RelPtr<NullString> name;
    BoneFlags flags;
    u32 jointId;
    u32 parentId;
    RelPtr<char> parentOffset;
    RelPtr<char> childOffset;
    RelPtr<char> previousSiblingOffset;
    RelPtr<char> nextSiblingOffset;
    float scale[3];
    float rotation[3];
    float translation[3];
    Matrix localMatrix;
    Matrix worldMatrix;
    Matrix inverseBaseMatrix;
    BillboardMode billboardMode;
    padding[8];
} [[inline]];

struct IndexStream {
    DataType formatType;
    u8 primitiveMode;
    bool isVisible;
    padding[2];
    ListPtr<u8> faceData;
    u32 bufferObject;
    u32 locationFlag;
    u32 commandCache;
    u32 commandCacheSize;
    u32 locationAddress;
    u32 memoryArea;
    RelPtr<char> boundingBoxOffset;
} [[inline]];

struct Primitive {
    ListPtr<RelPtr<IndexStream>> indexStreams;
    u32 nrBufferObjects;
    RelPtr<char> bufferObjectArrayOffset;
    u32 flags;
    u32 commandAllocator;
} [[inline]];

struct PrimitiveSet {
    ListPtr<u32> relatedBones;
    u32 skinningMode;
    ListPtr<RelPtr<Primitive>> primitives;
} [[inline]];

enum VertexAttributeType: u32 {
    VertexStream = 0x40000001,
    InterleavedVertexStream = 0x40000002,
    VertexParamAttribute = 0x80000000,
};

enum VertexAttributeUsage: u32 {
    Position,
    Normal,
    Tangent,
    Color,
    TextureCoordinate0,
    TextureCoordinate1,
    TextureCoordinate2,
    BoneIndex,
    BoneWeight,
    UserAttribute0,
    UserAttribute1,
    UserAttribute2,
    UserAttribute3,
    UserAttribute4,
    UserAttribute5,
    UserAttribute6,
    UserAttribute7,
    UserAttribute8,
    UserAttribute9,
    UserAttribute10,
    UserAttribute11,
    Interleave,
    Quantity
};

enum VertexAttributeFlags: u32 {
    None,
    VertexParam,
    Interleave,
    VertexParamAndInterleave,
};

using VertexAttribute;

struct VertexAttribute {
    VertexAttributeType type;
    VertexAttributeUsage usage;
    VertexAttributeFlags flags;
    if (type == VertexAttributeType::InterleavedVertexStream) {
        u32 bufferObject;
        u32 locationFlag;
        ListPtr<u8> vertexStreamData;
        u32 locationAddress;
        u32 memoryArea;
        u32 vertexDataEntrySize;
        ListPtr<RelPtr<VertexAttribute>> vertexStreams;
    } else if (type == VertexAttributeType::VertexStream) {
        u32 bufferObject;
        u32 locationFlag;
        ListPtr<u8> vertexStreamData;
        u32 locationAddress;
        u32 memoryArea;
        DataType formatType;
        u32 nrComponents;
        float scale;
        u32 offset;
    } else if (type == VertexAttributeType::VertexParamAttribute) {
        DataType formatType;
        u32 nrComponents;
        float scale;
        ListPtr<float> attributes;
    }
} [[inline]];

struct OrientedBoundingBox {
    u32 type;
    float centerPosition[3];
    OrientationMatrix orientationMatrix;
    float size[3];
} [[inline]];

struct SOBJMesh {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    u32 shapeIndex;
    u32 materialIndex;
    char *ownerModel: s32 [[pointer_base("std::ptr::relative_to_pointer")]];
    bool isVisible;
    u8 priority;
    u16 meshNodeVisibilityIndex;
    padding[4*18];
    RelPtr<NullString> meshNodeName;
} [[inline]];

bitfield SkeletonFlags {
    bool isModelCoordinate: 1;
    bool isTranslateAnimationEnabled: 1;
    padding: 30;
};

struct SOBJSkeleton {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    DictInfo<Bone> bones;
    RelPtr<Bone> rootBone;
    SkeletonScalingRule scalingRule;
    SkeletonFlags flags;
} [[inline]];

struct SOBJShape {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    u32 flags;
    RelPtr<OrientedBoundingBox> orientedBoundingBox;
    float positionOffset[3];
    u32 nrPrimitiveSets;
    RelPtr<PrimitiveSet> *primitiveSets[nrPrimitiveSets]: u32 [[pointer_base("std::ptr::relative_to_pointer")]];
    u32 baseAddress;
    u32 nrVertexAttributes;
    RelPtr<VertexAttribute> *vertexAttributes[nrVertexAttributes]: u32 [[pointer_base("std::ptr::relative_to_pointer")]];
    RelPtr<char> blendShapeOffset;
} [[inline]];

struct MeshNodeVisibilityCtr {
    RelPtr<NullString> name;
    u32 visible;
} [[inline]];

bitfield CMDLType {
    padding: 7;
    bool hasSkeleton: 1;
    padding: 24;
};

bitfield CMDLFlags1 {
    padding: 32;
};

bitfield CMDLFlags2 {
    bool isBranchVisible: 1;
    padding: 31;
};

bitfield CMDLFlags3 {
    bool isVisible: 1;
    padding: 8;
    bool isNonUniformScalable: 1;
    padding: 22;
};

struct CMDL {
    CMDLType type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> modelName;
    DictInfo<char> userData;
    CMDLFlags1 flags1;
    CMDLFlags2 flags2;
    u32 childCount;
    padding[4];
    DictInfo<GraphicsAnimationGroup> animationGroups;
    float transformScale[3];
    float transformRotate[3];
    float transformTranslate[3];
    Matrix localMatrix;
    Matrix worldMatrix;
    ListPtr<RelPtr<SOBJMesh>> meshes;
    DictInfo<MTOB> materials;
    ListPtr<RelPtr<SOBJShape>> shapes;
    DictInfo<MeshNodeVisibilityCtr> meshNodes;
    CMDLFlags3 flags3;
    CullMode cullMode;
    u32 layerId;
    if (type.hasSkeleton) {
        RelPtr<SOBJSkeleton> skeleton;
    }
} [[inline]];

enum PrimitiveType: u32 {
    Float,
    Int,
    Boolean,
    Vector2,
    Vector3,
    Transform,
    RgbaColor,
    Texture,
    BakedTransform,
    TransformMatrix,
};

enum RepeatMethod: u8 {
    Clamp,
    Repeat,
    MirroredRepeat,
    Repeat2, // functions exactly the same as Repeat
};

struct AnimationCurve {
    float startFrame, endFrame;
    RepeatMethod preRepeatMethod, postRepeatMethod;
    padding[2];
    u32 flags;
} [[inline]];

enum Interpolation: u32 {
    Nearest,
    Linear,
    CSpline,
};

enum QuantizationType: u32 {
    Hermite128,
    Hermite64,
    Hermite48,
    UnifiedHermite96,
    UnifiedHermite48,
    UnifiedHermite32,
    StepLinear64,
    StepLinear32
};

bitfield FloatSegmentFlags {
    bool single: 1;
    padding: 1;
    Interpolation interp: 3;
    QuantizationType quanti: 3;
    padding: 24;
};

struct Hermite128Key {
    float frame;
    float value;
    float inSlope;
    float outSlope;
};

struct Hermite64Key {
    u32 frameValue;
    u16 inSlope; // 8 fractional bits
    u16 outSlope; // 8 fractional bits
};

struct Hermite48Key {
    u8 frameValue[3];
    u8 inOutSlope[3];
};

struct UnifiedHermite96Key {
    float frame;
    float value;
    float inOutSlope;
};

struct UnifiedHermite48Key {
    u16 frame; // 8 fractional bits
    u16 value;
    u16 inOutSlope; // 8 fractional bits
};

struct UnifiedHermite32Key {
    u8 frame;
    u8 valueSlope[3];
};

struct StepLinear64Key {
    float frame;
    float value;
};

struct StepLinear32Key {
    u24 frame;
    u8 value;
};

struct FloatSegment {
    float startFrame, endFrame;
    FloatSegmentFlags flags;
    if (flags.single) {
        float singleValue;
    } else {
        u32 keyCount;
        float speed;
        if (flags.quanti != QuantizationType::Hermite128 && flags.quanti != QuantizationType::StepLinear64 && flags.quanti != QuantizationType::UnifiedHermite96) {
            float scale;
            float offset;
            float frame_scale;
        }
        match (flags.quanti) {
            (QuantizationType::Hermite128): Hermite128Key keys[keyCount];
            (QuantizationType::Hermite64): Hermite64Key keys[keyCount];
            (QuantizationType::Hermite48): Hermite48Key keys[keyCount];
            (QuantizationType::UnifiedHermite96): UnifiedHermite96Key keys[keyCount];
            (QuantizationType::UnifiedHermite48): UnifiedHermite48Key keys[keyCount];
            (QuantizationType::UnifiedHermite32): UnifiedHermite32Key keys[keyCount];
            (QuantizationType::StepLinear64): StepLinear64Key keys[keyCount];
            (QuantizationType::StepLinear32): StepLinear32Key keys[keyCount];
        }
    }
} [[inline]];

struct FloatAnimationCurve {
    AnimationCurve curve;
    u32 nrSegments;
    RelPtr<FloatSegment> segments[nrSegments];
} [[inline]];

struct Vec3 {
    float x, y, z;
};

struct Vector3AndFlags {
    Vec3 value;
    u32 flags;
};

struct Vector3Curve {
    AnimationCurve curve;
    if (curve.flags & 1) {
        float constantValue[3];
        u32 constantFlags;
    } else {
        Vector3AndFlags values[curve.endFrame - curve.startFrame];
    }
} [[inline]];

struct QuaternionAndFlags {
    float value[4];
    u32 flags;
};

struct QuaternionCurve {
    AnimationCurve curve;
    // TODO: what if flags & 1?
    QuaternionAndFlags values[curve.endFrame - curve.startFrame];
} [[inline]];

struct CANMBone {
    u32 flags;
    RelPtr<NullString> bonePath;
    // these next two appear to be unused
    RelPtr<NullString> unknown1;
    RelPtr<NullString> unknown2;
    PrimitiveType primitiveType;
    if (primitiveType == PrimitiveType::Vector2) {
        if (flags & 1) float xConst;
        else if (flags & 4) padding[4];
        else RelPtr<FloatAnimationCurve> xVal;
        if (flags & 2) float yConst;
        else if (flags & 8) padding[4];
        else RelPtr<FloatAnimationCurve> yVal;
    } else if (primitiveType == PrimitiveType::Transform) {
        if (flags & 0x10000) padding[4];
        else if (flags & 0x40) float scale_x;
        else RelPtr<FloatAnimationCurve> scale_x;

        if (flags & 0x20000) padding[4];
        else if (flags & 0x80) float scale_y;
        else RelPtr<FloatAnimationCurve> scale_y;
        
        if (flags & 0x40000) padding[4];
        else if (flags & 0x100) float scale_z;
        else RelPtr<FloatAnimationCurve> scale_z;
        
        if (flags & 0x80000) padding[4];
        else if (flags & 0x200) float rot_x;
        else RelPtr<FloatAnimationCurve> rot_x;
        
        if (flags & 0x100000) padding[4];
        else if (flags & 0x400) float rot_y;
        else RelPtr<FloatAnimationCurve> rot_y;
        
        if (flags & 0x200000) padding[4];
        else if (flags & 0x800) float rot_z;
        else RelPtr<FloatAnimationCurve> rot_z;

        padding[4];
        
        if (flags & 0x800000) padding[4];
        else if (flags & 0x2000) float pos_x;
        else RelPtr<FloatAnimationCurve> pos_x;
        
        if (flags & 0x1000000) padding[4];
        else if (flags & 0x4000) float pos_y;
        else RelPtr<FloatAnimationCurve> pos_y;
        
        if (flags & 0x2000000) padding[4];
        else if (flags & 0x8000) float pos_z;
        else RelPtr<FloatAnimationCurve> pos_z;
    } else if (primitiveType == PrimitiveType::BakedTransform) {
        if (flags & 0x10) padding[4];
        else RelPtr<QuaternionCurve> rotationOffset;
        if (flags & 0x08) padding[4];
        else RelPtr<Vector3Curve> translation;
        if (flags & 0x20) padding[4];
        else RelPtr<Vector3Curve> scale;
    } else if (primitiveType == PrimitiveType::RgbaColor) {
        if (flags & 0x10) {
            padding[4];
        } else {
            RelPtr<FloatAnimationCurve> redCurve;
        }
        if (flags & 0x20) {
            padding[4];
        } else {
            RelPtr<FloatAnimationCurve> greenCurve;
        }
        if (flags & 0x40) {
            padding[4];
        } else {
            RelPtr<FloatAnimationCurve> blueCurve;
        }
        if (flags & 0x80) {
            padding[4];
        } else {
            RelPtr<FloatAnimationCurve> alphaCurve;
        }
    }
} [[inline]];

struct CANM {
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    RelPtr<NullString> targetAnimationGroupName;
    LoopMode loopMode;
    float frameSize;
    DictInfo<CANMBone> memberAnimationsData;
    DictInfo<char> userData;
} [[inline]];

struct LightAnimationChannel {
    padding[4];
    RelPtr<NullString> name;
}[[inline]];

struct LightAnimation {
    u32 flags;
    padding[4];
    RelPtr<NullString> name;
    padding[4];
    DictInfo<LightAnimationChannel> channels;
    ListPtr<u32> some_enum;
} [[inline]];

struct LightLutOrSomething {
    u32 flags;
    padding[8];
    RelPtr<LutTable> info;
} [[inline]];

struct SomeMoreLightData {
    padding[8];
    RelPtr<LightLutOrSomething> lut;
};

enum LightType: u32 {
    Positional,
    Directional,
};

struct CFLT {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    u32 someFlags;
    padding[3*4];
    DictInfo<GraphicsAnimationGroup> animation;
    Vec3 scale;
    Vec3 rotation;
    Vec3 transform;
    Matrix localMatrix;
    Matrix worldMatrix;
    u32 enabled;
    LightType lightType;
    ColorFloat ambientF;
    ColorFloat diffuseF;
    ColorFloat specularF[2];
    u32 ambientB;
    u32 diffuseB;
    u32 specularB[2];
    float positionOrDirection[3];
    RelPtr<ReferenceLookupTableCtr> attenutationLut;
    RelPtr<ReferenceLookupTableCtr> spotlightLut;
    padding[2*4];
    u32 attenuationScale; // float1.7.12
    u32 attenuationBias; // float1.7.12
} [[inline]];

struct ViewMatrixData {
    u32;
    u32 flags;
    Vec3 lookAt;
    float angle;
} [[inline]];

struct ProjectionMatrixData {
    u32;
    float znear;
    float zfar;
    float aspect;
    float fov;
} [[inline]];

struct CCAM {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    padding[16];
    DictInfo<GraphicsAnimationGroup> animationGroups;
    Vec3 scale;
    Vec3 rotation;
    Vec3 transform;
    Matrix localMatrix;
    Matrix worldMatrix;
    u32;
    u32;
    RelPtr<ViewMatrixData>;
    RelPtr<ProjectionMatrixData>;
} [[inline]];

struct CENVCamera {
    u32;
    RelPtr<NullString>;
    u32;
} [[inline]];

struct CENVLight {
    u32;
    RelPtr<NullString>;
    u32;
} [[inline]];

struct CENVLightSet {
    u32;
    ListPtr<RelPtr<CENVLight>> lights;
} [[inline]];

struct CENV {
    u32 type;
    char magic[4];
    u32 revision;
    RelPtr<NullString> name;
    DictInfo<char> userData;
    ListPtr<RelPtr<CENVCamera>> cameras;
    ListPtr<RelPtr<CENVLightSet>> lightSets;
    ListPtr<char>;
} [[inline]];

struct DATA {
    char magic[4];
    u32 sectionSize;
    DictInfo<CMDL> models;
    DictInfo<TXOB> textures;
    DictInfo<LUTS> lookUpTables;
    DictInfo<char> materials;
    DictInfo<SHDR> shaders;
    DictInfo<CCAM> cameras;
    DictInfo<CFLT> lights;
    DictInfo<char> fogs;
    DictInfo<CENV> scenes;
    DictInfo<CANM> skeletalAnimations;
    DictInfo<CANM> materialAnimations;
    DictInfo<CANM> visibilityAnimations;
    DictInfo<char> cameraAnimations;
    DictInfo<char> lightAnimations;
    DictInfo<char> emitters;
};

struct CGFX {
    CGFXHeader header;
    DATA data;
};

CGFX cgfx @ 0;
