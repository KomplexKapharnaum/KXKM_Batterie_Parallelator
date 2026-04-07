#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSString.h>
#import <Foundation/NSValue.h>

@class SharedAuditEvent, SharedAuditEventCompanion, SharedAuditUseCase, SharedAudit_events, SharedAuthUseCase, SharedBatteryHealth, SharedBatteryHealthCompanion, SharedBatteryHistoryPoint, SharedBatteryState, SharedBatteryStateCompanion, SharedBatteryStatus, SharedBatteryStatusCompanion, SharedBattery_history, SharedBleTransportCompanion, SharedBmuDatabaseCompanion, SharedBmuDatabaseQueries, SharedCloudRestClient, SharedCommandResult, SharedCommandResultCompanion, SharedConfigUseCase, SharedControlUseCase, SharedDatabaseHelper, SharedDiagnostic, SharedDiagnosticCompanion, SharedDiagnosticSeverity, SharedDiagnosticSeverityCompanion, SharedDiagnostics, SharedDriverFactory, SharedFleetDiagnostic, SharedFleetDiagnosticCompanion, SharedFleetHealth, SharedFleetHealthCompanion, SharedFleet_health, SharedGattParser, SharedGattParserRintResult, SharedKotlinArray<T>, SharedKotlinByteArray, SharedKotlinByteIterator, SharedKotlinEnum<E>, SharedKotlinEnumCompanion, SharedKotlinException, SharedKotlinIllegalStateException, SharedKotlinNothing, SharedKotlinRuntimeException, SharedKotlinThrowable, SharedKotlinTriple<__covariant A, __covariant B, __covariant C>, SharedKotlinUnit, SharedKotlinx_serialization_coreSerialKind, SharedKotlinx_serialization_coreSerializersModule, SharedMlScore, SharedMlScoreCompanion, SharedMl_scores, SharedMonitoringUseCase, SharedMqttTransport, SharedPendingAuditSyncItem, SharedPinHasher, SharedProtectionConfig, SharedProtectionConfigCompanion, SharedRuntimeAfterVersion, SharedRuntimeBaseTransacterImpl, SharedRuntimeExecutableQuery<__covariant RowType>, SharedRuntimeQuery<__covariant RowType>, SharedRuntimeTransacterImpl, SharedRuntimeTransacterTransaction, SharedSharedFactory, SharedSharedFactoryCompanion, SharedSohRestClient, SharedSohUseCase, SharedSohUseCaseCompanion, SharedSolarData, SharedSolarDataCompanion, SharedSyncManager, SharedSync_queue, SharedSystemInfo, SharedSystemInfoCompanion, SharedTransportCapability, SharedTransportChannel, SharedTransportManager, SharedTransportManagerCompanion, SharedUserProfile, SharedUserProfileCompanion, SharedUserRole, SharedUserRoleCompanion, SharedUser_profiles, SharedWifiStatusInfo, SharedWifiStatusInfoCompanion, SharedWifiTransport;

@protocol SharedBmuDatabase, SharedKotlinAnnotation, SharedKotlinComparable, SharedKotlinIterator, SharedKotlinKAnnotatedElement, SharedKotlinKClass, SharedKotlinKClassifier, SharedKotlinKDeclarationContainer, SharedKotlinx_coroutines_coreFlow, SharedKotlinx_coroutines_coreFlowCollector, SharedKotlinx_coroutines_coreSharedFlow, SharedKotlinx_coroutines_coreStateFlow, SharedKotlinx_serialization_coreCompositeDecoder, SharedKotlinx_serialization_coreCompositeEncoder, SharedKotlinx_serialization_coreDecoder, SharedKotlinx_serialization_coreDeserializationStrategy, SharedKotlinx_serialization_coreEncoder, SharedKotlinx_serialization_coreKSerializer, SharedKotlinx_serialization_coreSerialDescriptor, SharedKotlinx_serialization_coreSerializationStrategy, SharedKotlinx_serialization_coreSerializersModuleCollector, SharedRuntimeCloseable, SharedRuntimeQueryListener, SharedRuntimeQueryResult, SharedRuntimeSqlCursor, SharedRuntimeSqlDriver, SharedRuntimeSqlPreparedStatement, SharedRuntimeSqlSchema, SharedRuntimeTransacter, SharedRuntimeTransacterBase, SharedRuntimeTransactionCallbacks, SharedRuntimeTransactionWithReturn, SharedRuntimeTransactionWithoutReturn, SharedSyncCloud, SharedSyncStore, SharedTransport;

NS_ASSUME_NONNULL_BEGIN
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wincompatible-property-type"
#pragma clang diagnostic ignored "-Wnullability"

#pragma push_macro("_Nullable_result")
#if !__has_feature(nullability_nullable_result)
#undef _Nullable_result
#define _Nullable_result _Nullable
#endif

__attribute__((swift_name("KotlinBase")))
@interface SharedBase : NSObject
- (instancetype)init __attribute__((unavailable));
+ (instancetype)new __attribute__((unavailable));
+ (void)initialize __attribute__((objc_requires_super));
@end

@interface SharedBase (SharedBaseCopying) <NSCopying>
@end

__attribute__((swift_name("KotlinMutableSet")))
@interface SharedMutableSet<ObjectType> : NSMutableSet<ObjectType>
@end

__attribute__((swift_name("KotlinMutableDictionary")))
@interface SharedMutableDictionary<KeyType, ObjectType> : NSMutableDictionary<KeyType, ObjectType>
@end

@interface NSError (NSErrorSharedKotlinException)
@property (readonly) id _Nullable kotlinException;
@end

__attribute__((swift_name("KotlinNumber")))
@interface SharedNumber : NSNumber
- (instancetype)initWithChar:(char)value __attribute__((unavailable));
- (instancetype)initWithUnsignedChar:(unsigned char)value __attribute__((unavailable));
- (instancetype)initWithShort:(short)value __attribute__((unavailable));
- (instancetype)initWithUnsignedShort:(unsigned short)value __attribute__((unavailable));
- (instancetype)initWithInt:(int)value __attribute__((unavailable));
- (instancetype)initWithUnsignedInt:(unsigned int)value __attribute__((unavailable));
- (instancetype)initWithLong:(long)value __attribute__((unavailable));
- (instancetype)initWithUnsignedLong:(unsigned long)value __attribute__((unavailable));
- (instancetype)initWithLongLong:(long long)value __attribute__((unavailable));
- (instancetype)initWithUnsignedLongLong:(unsigned long long)value __attribute__((unavailable));
- (instancetype)initWithFloat:(float)value __attribute__((unavailable));
- (instancetype)initWithDouble:(double)value __attribute__((unavailable));
- (instancetype)initWithBool:(BOOL)value __attribute__((unavailable));
- (instancetype)initWithInteger:(NSInteger)value __attribute__((unavailable));
- (instancetype)initWithUnsignedInteger:(NSUInteger)value __attribute__((unavailable));
+ (instancetype)numberWithChar:(char)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedChar:(unsigned char)value __attribute__((unavailable));
+ (instancetype)numberWithShort:(short)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedShort:(unsigned short)value __attribute__((unavailable));
+ (instancetype)numberWithInt:(int)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedInt:(unsigned int)value __attribute__((unavailable));
+ (instancetype)numberWithLong:(long)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedLong:(unsigned long)value __attribute__((unavailable));
+ (instancetype)numberWithLongLong:(long long)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedLongLong:(unsigned long long)value __attribute__((unavailable));
+ (instancetype)numberWithFloat:(float)value __attribute__((unavailable));
+ (instancetype)numberWithDouble:(double)value __attribute__((unavailable));
+ (instancetype)numberWithBool:(BOOL)value __attribute__((unavailable));
+ (instancetype)numberWithInteger:(NSInteger)value __attribute__((unavailable));
+ (instancetype)numberWithUnsignedInteger:(NSUInteger)value __attribute__((unavailable));
@end

__attribute__((swift_name("KotlinByte")))
@interface SharedByte : SharedNumber
- (instancetype)initWithChar:(char)value;
+ (instancetype)numberWithChar:(char)value;
@end

__attribute__((swift_name("KotlinUByte")))
@interface SharedUByte : SharedNumber
- (instancetype)initWithUnsignedChar:(unsigned char)value;
+ (instancetype)numberWithUnsignedChar:(unsigned char)value;
@end

__attribute__((swift_name("KotlinShort")))
@interface SharedShort : SharedNumber
- (instancetype)initWithShort:(short)value;
+ (instancetype)numberWithShort:(short)value;
@end

__attribute__((swift_name("KotlinUShort")))
@interface SharedUShort : SharedNumber
- (instancetype)initWithUnsignedShort:(unsigned short)value;
+ (instancetype)numberWithUnsignedShort:(unsigned short)value;
@end

__attribute__((swift_name("KotlinInt")))
@interface SharedInt : SharedNumber
- (instancetype)initWithInt:(int)value;
+ (instancetype)numberWithInt:(int)value;
@end

__attribute__((swift_name("KotlinUInt")))
@interface SharedUInt : SharedNumber
- (instancetype)initWithUnsignedInt:(unsigned int)value;
+ (instancetype)numberWithUnsignedInt:(unsigned int)value;
@end

__attribute__((swift_name("KotlinLong")))
@interface SharedLong : SharedNumber
- (instancetype)initWithLongLong:(long long)value;
+ (instancetype)numberWithLongLong:(long long)value;
@end

__attribute__((swift_name("KotlinULong")))
@interface SharedULong : SharedNumber
- (instancetype)initWithUnsignedLongLong:(unsigned long long)value;
+ (instancetype)numberWithUnsignedLongLong:(unsigned long long)value;
@end

__attribute__((swift_name("KotlinFloat")))
@interface SharedFloat : SharedNumber
- (instancetype)initWithFloat:(float)value;
+ (instancetype)numberWithFloat:(float)value;
@end

__attribute__((swift_name("KotlinDouble")))
@interface SharedDouble : SharedNumber
- (instancetype)initWithDouble:(double)value;
+ (instancetype)numberWithDouble:(double)value;
@end

__attribute__((swift_name("KotlinBoolean")))
@interface SharedBoolean : SharedNumber
- (instancetype)initWithBool:(BOOL)value;
+ (instancetype)numberWithBool:(BOOL)value;
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("IOSPlatform")))
@interface SharedIOSPlatform : SharedBase
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
@property (readonly) NSString *name __attribute__((swift_name("name")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SharedFactory")))
@interface SharedSharedFactory : SharedBase
- (instancetype)initWithDriverFactory:(SharedDriverFactory *)driverFactory __attribute__((swift_name("init(driverFactory:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedSharedFactoryCompanion *companion __attribute__((swift_name("companion")));
- (void)close __attribute__((swift_name("close()")));
- (void)configureCloudApiUrl:(NSString *)apiUrl apiKey:(NSString *)apiKey mqttBroker:(NSString *)mqttBroker mqttUser:(NSString *)mqttUser mqttPass:(NSString *)mqttPass __attribute__((swift_name("configureCloud(apiUrl:apiKey:mqttBroker:mqttUser:mqttPass:)")));
- (void)configureWifiBaseUrl:(NSString *)baseUrl token:(NSString *)token __attribute__((swift_name("configureWifi(baseUrl:token:)")));
- (SharedConfigUseCase *)createConfigUseCase __attribute__((swift_name("createConfigUseCase()")));
- (SharedControlUseCase *)createControlUseCase __attribute__((swift_name("createControlUseCase()")));
- (SharedMonitoringUseCase *)createMonitoringUseCase __attribute__((swift_name("createMonitoringUseCase()")));
- (SharedSyncManager *)createSyncManager __attribute__((swift_name("createSyncManager()")));
- (void)onCloudMqttMessageTopic:(NSString *)topic payload:(NSString *)payload __attribute__((swift_name("onCloudMqttMessage(topic:payload:)")));
- (void)setCurrentUserUserId:(NSString *)userId __attribute__((swift_name("setCurrentUser(userId:)")));
- (void)setMqttConnectedConnected:(BOOL)connected __attribute__((swift_name("setMqttConnected(connected:)")));
@property (readonly) SharedAuditUseCase *auditUseCase __attribute__((swift_name("auditUseCase")));
@property (readonly) SharedAuthUseCase *authUseCase __attribute__((swift_name("authUseCase")));
@property (readonly) SharedSohRestClient *sohRestClient __attribute__((swift_name("sohRestClient")));
@property (readonly) SharedSohUseCase *sohUseCase __attribute__((swift_name("sohUseCase")));
@property (readonly) SharedTransportManager *transportManager __attribute__((swift_name("transportManager")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SharedFactory.Companion")))
@interface SharedSharedFactoryCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedSharedFactoryCompanion *shared __attribute__((swift_name("shared")));
- (SharedSharedFactory *)createDriverFactory:(SharedDriverFactory *)driverFactory __attribute__((swift_name("create(driverFactory:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("AuthUseCase")))
@interface SharedAuthUseCase : SharedBase
- (instancetype)initWithDb:(SharedDatabaseHelper *)db __attribute__((swift_name("init(db:)"))) __attribute__((objc_designated_initializer));
- (SharedUserProfile * _Nullable)authenticatePin:(NSString *)pin __attribute__((swift_name("authenticate(pin:)")));
- (void)createUserName:(NSString *)name pin:(NSString *)pin role:(SharedUserRole *)role __attribute__((swift_name("createUser(name:pin:role:)")));
- (void)deleteUserUserId:(NSString *)userId __attribute__((swift_name("deleteUser(userId:)")));
- (NSArray<SharedUserProfile *> *)getAllUsers __attribute__((swift_name("getAllUsers()")));
- (BOOL)hasNoUsers __attribute__((swift_name("hasNoUsers()")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("PinHasher")))
@interface SharedPinHasher : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)pinHasher __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedPinHasher *shared __attribute__((swift_name("shared")));
- (NSString *)generateSalt __attribute__((swift_name("generateSalt()")));
- (NSString *)hashPin:(NSString *)pin salt:(NSString *)salt __attribute__((swift_name("hash(pin:salt:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Audit_events")))
@interface SharedAudit_events : SharedBase
- (instancetype)initWithId:(int64_t)id timestamp:(int64_t)timestamp user_id:(NSString *)user_id action:(NSString *)action target:(SharedLong * _Nullable)target detail:(NSString * _Nullable)detail synced:(int64_t)synced __attribute__((swift_name("init(id:timestamp:user_id:action:target:detail:synced:)"))) __attribute__((objc_designated_initializer));
- (SharedAudit_events *)doCopyId:(int64_t)id timestamp:(int64_t)timestamp user_id:(NSString *)user_id action:(NSString *)action target:(SharedLong * _Nullable)target detail:(NSString * _Nullable)detail synced:(int64_t)synced __attribute__((swift_name("doCopy(id:timestamp:user_id:action:target:detail:synced:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *action __attribute__((swift_name("action")));
@property (readonly) NSString * _Nullable detail __attribute__((swift_name("detail")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) int64_t synced __attribute__((swift_name("synced")));
@property (readonly) SharedLong * _Nullable target __attribute__((swift_name("target")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@property (readonly) NSString *user_id __attribute__((swift_name("user_id")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryHistoryPoint")))
@interface SharedBatteryHistoryPoint : SharedBase
- (instancetype)initWithTimestamp:(int64_t)timestamp voltageMv:(int32_t)voltageMv currentMa:(int32_t)currentMa __attribute__((swift_name("init(timestamp:voltageMv:currentMa:)"))) __attribute__((objc_designated_initializer));
- (SharedBatteryHistoryPoint *)doCopyTimestamp:(int64_t)timestamp voltageMv:(int32_t)voltageMv currentMa:(int32_t)currentMa __attribute__((swift_name("doCopy(timestamp:voltageMv:currentMa:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t currentMa __attribute__((swift_name("currentMa")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@property (readonly) int32_t voltageMv __attribute__((swift_name("voltageMv")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Battery_history")))
@interface SharedBattery_history : SharedBase
- (instancetype)initWithId:(int64_t)id timestamp:(int64_t)timestamp battery_index:(int64_t)battery_index voltage_mv:(int64_t)voltage_mv current_ma:(int64_t)current_ma state:(NSString *)state ah_discharge_mah:(int64_t)ah_discharge_mah ah_charge_mah:(int64_t)ah_charge_mah __attribute__((swift_name("init(id:timestamp:battery_index:voltage_mv:current_ma:state:ah_discharge_mah:ah_charge_mah:)"))) __attribute__((objc_designated_initializer));
- (SharedBattery_history *)doCopyId:(int64_t)id timestamp:(int64_t)timestamp battery_index:(int64_t)battery_index voltage_mv:(int64_t)voltage_mv current_ma:(int64_t)current_ma state:(NSString *)state ah_discharge_mah:(int64_t)ah_discharge_mah ah_charge_mah:(int64_t)ah_charge_mah __attribute__((swift_name("doCopy(id:timestamp:battery_index:voltage_mv:current_ma:state:ah_discharge_mah:ah_charge_mah:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int64_t ah_charge_mah __attribute__((swift_name("ah_charge_mah")));
@property (readonly) int64_t ah_discharge_mah __attribute__((swift_name("ah_discharge_mah")));
@property (readonly) int64_t battery_index __attribute__((swift_name("battery_index")));
@property (readonly) int64_t current_ma __attribute__((swift_name("current_ma")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) NSString *state __attribute__((swift_name("state")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@property (readonly) int64_t voltage_mv __attribute__((swift_name("voltage_mv")));
@end

__attribute__((swift_name("RuntimeTransacterBase")))
@protocol SharedRuntimeTransacterBase
@required
@end

__attribute__((swift_name("RuntimeTransacter")))
@protocol SharedRuntimeTransacter <SharedRuntimeTransacterBase>
@required
- (void)transactionNoEnclosing:(BOOL)noEnclosing body:(void (^)(id<SharedRuntimeTransactionWithoutReturn>))body __attribute__((swift_name("transaction(noEnclosing:body:)")));
- (id _Nullable)transactionWithResultNoEnclosing:(BOOL)noEnclosing bodyWithReturn:(id _Nullable (^)(id<SharedRuntimeTransactionWithReturn>))bodyWithReturn __attribute__((swift_name("transactionWithResult(noEnclosing:bodyWithReturn:)")));
@end

__attribute__((swift_name("BmuDatabase")))
@protocol SharedBmuDatabase <SharedRuntimeTransacter>
@required
@property (readonly) SharedBmuDatabaseQueries *bmuDatabaseQueries __attribute__((swift_name("bmuDatabaseQueries")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BmuDatabaseCompanion")))
@interface SharedBmuDatabaseCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedBmuDatabaseCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedBmuDatabase>)invokeDriver:(id<SharedRuntimeSqlDriver>)driver __attribute__((swift_name("invoke(driver:)")));
@property (readonly) id<SharedRuntimeSqlSchema> Schema __attribute__((swift_name("Schema")));
@end

__attribute__((swift_name("RuntimeBaseTransacterImpl")))
@interface SharedRuntimeBaseTransacterImpl : SharedBase
- (instancetype)initWithDriver:(id<SharedRuntimeSqlDriver>)driver __attribute__((swift_name("init(driver:)"))) __attribute__((objc_designated_initializer));

/**
 * @note This method has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
- (NSString *)createArgumentsCount:(int32_t)count __attribute__((swift_name("createArguments(count:)")));

/**
 * @note This method has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
- (void)notifyQueriesIdentifier:(int32_t)identifier tableProvider:(void (^)(SharedKotlinUnit *(^)(NSString *)))tableProvider __attribute__((swift_name("notifyQueries(identifier:tableProvider:)")));

/**
 * @note This method has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
- (id _Nullable)postTransactionCleanupTransaction:(SharedRuntimeTransacterTransaction *)transaction enclosing:(SharedRuntimeTransacterTransaction * _Nullable)enclosing thrownException:(SharedKotlinThrowable * _Nullable)thrownException returnValue:(id _Nullable)returnValue __attribute__((swift_name("postTransactionCleanup(transaction:enclosing:thrownException:returnValue:)")));

/**
 * @note This property has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
@property (readonly) id<SharedRuntimeSqlDriver> driver __attribute__((swift_name("driver")));
@end

__attribute__((swift_name("RuntimeTransacterImpl")))
@interface SharedRuntimeTransacterImpl : SharedRuntimeBaseTransacterImpl <SharedRuntimeTransacter>
- (instancetype)initWithDriver:(id<SharedRuntimeSqlDriver>)driver __attribute__((swift_name("init(driver:)"))) __attribute__((objc_designated_initializer));
- (void)transactionNoEnclosing:(BOOL)noEnclosing body:(void (^)(id<SharedRuntimeTransactionWithoutReturn>))body __attribute__((swift_name("transaction(noEnclosing:body:)")));
- (id _Nullable)transactionWithResultNoEnclosing:(BOOL)noEnclosing bodyWithReturn:(id _Nullable (^)(id<SharedRuntimeTransactionWithReturn>))bodyWithReturn __attribute__((swift_name("transactionWithResult(noEnclosing:bodyWithReturn:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BmuDatabaseQueries")))
@interface SharedBmuDatabaseQueries : SharedRuntimeTransacterImpl
- (instancetype)initWithDriver:(id<SharedRuntimeSqlDriver>)driver __attribute__((swift_name("init(driver:)"))) __attribute__((objc_designated_initializer));
- (SharedRuntimeQuery<SharedLong *> *)countPendingSync __attribute__((swift_name("countPendingSync()")));
- (SharedRuntimeQuery<SharedLong *> *)countUnsyncedAudit __attribute__((swift_name("countUnsyncedAudit()")));
- (SharedRuntimeQuery<SharedLong *> *)countUsers __attribute__((swift_name("countUsers()")));
- (void)deleteSyncItemsId:(int64_t)id __attribute__((swift_name("deleteSyncItems(id:)")));
- (void)deleteUserId:(NSString *)id __attribute__((swift_name("deleteUser(id:)")));
- (SharedRuntimeQuery<SharedSync_queue *> *)dequeueSyncValue_:(int64_t)value_ __attribute__((swift_name("dequeueSync(value_:)")));
- (SharedRuntimeQuery<id> *)dequeueSyncValue:(int64_t)value mapper:(id (^)(SharedLong *, NSString *, NSString *, SharedLong *, SharedLong *))mapper __attribute__((swift_name("dequeueSync(value:mapper:)")));
- (void)enqueueSyncType:(NSString *)type payload:(NSString *)payload created_at:(int64_t)created_at __attribute__((swift_name("enqueueSync(type:payload:created_at:)")));
- (SharedRuntimeQuery<SharedDiagnostics *> *)getAllDiagnostics __attribute__((swift_name("getAllDiagnostics()")));
- (SharedRuntimeQuery<id> *)getAllDiagnosticsMapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong *))mapper __attribute__((swift_name("getAllDiagnostics(mapper:)")));
- (SharedRuntimeQuery<SharedUser_profiles *> *)getAllUsers __attribute__((swift_name("getAllUsers()")));
- (SharedRuntimeQuery<id> *)getAllUsersMapper:(id (^)(NSString *, NSString *, NSString *, NSString *, NSString *))mapper __attribute__((swift_name("getAllUsers(mapper:)")));
- (SharedRuntimeQuery<SharedAudit_events *> *)getAuditByActionAction:(NSString *)action value_:(int64_t)value_ __attribute__((swift_name("getAuditByAction(action:value_:)")));
- (SharedRuntimeQuery<id> *)getAuditByActionAction:(NSString *)action value:(int64_t)value mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong * _Nullable, NSString * _Nullable, SharedLong *))mapper __attribute__((swift_name("getAuditByAction(action:value:mapper:)")));
- (SharedRuntimeQuery<SharedAudit_events *> *)getAuditByActionAndTargetAction:(NSString *)action target:(SharedLong * _Nullable)target value_:(int64_t)value_ __attribute__((swift_name("getAuditByActionAndTarget(action:target:value_:)")));
- (SharedRuntimeQuery<id> *)getAuditByActionAndTargetAction:(NSString *)action target:(SharedLong * _Nullable)target value:(int64_t)value mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong * _Nullable, NSString * _Nullable, SharedLong *))mapper __attribute__((swift_name("getAuditByActionAndTarget(action:target:value:mapper:)")));
- (SharedRuntimeQuery<SharedAudit_events *> *)getAuditByTargetTarget:(SharedLong * _Nullable)target value_:(int64_t)value_ __attribute__((swift_name("getAuditByTarget(target:value_:)")));
- (SharedRuntimeQuery<id> *)getAuditByTargetTarget:(SharedLong * _Nullable)target value:(int64_t)value mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong * _Nullable, NSString * _Nullable, SharedLong *))mapper __attribute__((swift_name("getAuditByTarget(target:value:mapper:)")));
- (SharedRuntimeQuery<SharedAudit_events *> *)getAuditEventsValue_:(int64_t)value_ __attribute__((swift_name("getAuditEvents(value_:)")));
- (SharedRuntimeQuery<id> *)getAuditEventsValue:(int64_t)value mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong * _Nullable, NSString * _Nullable, SharedLong *))mapper __attribute__((swift_name("getAuditEvents(value:mapper:)")));
- (SharedRuntimeQuery<SharedDiagnostics *> *)getDiagnosticBattery_index:(int64_t)battery_index __attribute__((swift_name("getDiagnostic(battery_index:)")));
- (SharedRuntimeQuery<id> *)getDiagnosticBattery_index:(int64_t)battery_index mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong *))mapper __attribute__((swift_name("getDiagnostic(battery_index:mapper:)")));
- (SharedRuntimeQuery<SharedBattery_history *> *)getHistoryBattery_index:(int64_t)battery_index timestamp:(int64_t)timestamp __attribute__((swift_name("getHistory(battery_index:timestamp:)")));
- (SharedRuntimeQuery<id> *)getHistoryBattery_index:(int64_t)battery_index timestamp:(int64_t)timestamp mapper:(id (^)(SharedLong *, SharedLong *, SharedLong *, SharedLong *, SharedLong *, NSString *, SharedLong *, SharedLong *))mapper __attribute__((swift_name("getHistory(battery_index:timestamp:mapper:)")));
- (SharedRuntimeQuery<SharedBattery_history *> *)getLastKnownBatteries __attribute__((swift_name("getLastKnownBatteries()")));
- (SharedRuntimeQuery<id> *)getLastKnownBatteriesMapper:(id (^)(SharedLong *, SharedLong *, SharedLong *, SharedLong *, SharedLong *, NSString *, SharedLong *, SharedLong *))mapper __attribute__((swift_name("getLastKnownBatteries(mapper:)")));
- (SharedRuntimeQuery<SharedFleet_health *> *)getLatestFleetHealth __attribute__((swift_name("getLatestFleetHealth()")));
- (SharedRuntimeQuery<id> *)getLatestFleetHealthMapper:(id (^)(SharedLong *, SharedDouble *, SharedLong *, SharedDouble *, SharedDouble *, SharedLong *))mapper __attribute__((swift_name("getLatestFleetHealth(mapper:)")));
- (SharedRuntimeQuery<SharedMl_scores *> *)getLatestMlScores __attribute__((swift_name("getLatestMlScores()")));
- (SharedRuntimeQuery<id> *)getLatestMlScoresMapper:(id (^)(SharedLong *, SharedLong *, SharedDouble *, SharedLong *, SharedDouble *, SharedDouble *, SharedLong *))mapper __attribute__((swift_name("getLatestMlScores(mapper:)")));
- (SharedRuntimeQuery<SharedMl_scores *> *)getMlScoreBattery_index:(int64_t)battery_index __attribute__((swift_name("getMlScore(battery_index:)")));
- (SharedRuntimeQuery<id> *)getMlScoreBattery_index:(int64_t)battery_index mapper:(id (^)(SharedLong *, SharedLong *, SharedDouble *, SharedLong *, SharedDouble *, SharedDouble *, SharedLong *))mapper __attribute__((swift_name("getMlScore(battery_index:mapper:)")));
- (SharedRuntimeQuery<SharedAudit_events *> *)getUnsyncedAuditValue_:(int64_t)value_ __attribute__((swift_name("getUnsyncedAudit(value_:)")));
- (SharedRuntimeQuery<id> *)getUnsyncedAuditValue:(int64_t)value mapper:(id (^)(SharedLong *, SharedLong *, NSString *, NSString *, SharedLong * _Nullable, NSString * _Nullable, SharedLong *))mapper __attribute__((swift_name("getUnsyncedAudit(value:mapper:)")));
- (SharedRuntimeQuery<SharedUser_profiles *> *)getUserByHashPin_hash:(NSString *)pin_hash __attribute__((swift_name("getUserByHash(pin_hash:)")));
- (SharedRuntimeQuery<id> *)getUserByHashPin_hash:(NSString *)pin_hash mapper:(id (^)(NSString *, NSString *, NSString *, NSString *, NSString *))mapper __attribute__((swift_name("getUserByHash(pin_hash:mapper:)")));
- (void)insertAuditTimestamp:(int64_t)timestamp user_id:(NSString *)user_id action:(NSString *)action target:(SharedLong * _Nullable)target detail:(NSString * _Nullable)detail __attribute__((swift_name("insertAudit(timestamp:user_id:action:target:detail:)")));
- (void)insertHistoryTimestamp:(int64_t)timestamp battery_index:(int64_t)battery_index voltage_mv:(int64_t)voltage_mv current_ma:(int64_t)current_ma state:(NSString *)state ah_discharge_mah:(int64_t)ah_discharge_mah ah_charge_mah:(int64_t)ah_charge_mah __attribute__((swift_name("insertHistory(timestamp:battery_index:voltage_mv:current_ma:state:ah_discharge_mah:ah_charge_mah:)")));
- (void)insertUserId:(NSString *)id name:(NSString *)name role:(NSString *)role pin_hash:(NSString *)pin_hash salt:(NSString *)salt __attribute__((swift_name("insertUser(id:name:role:pin_hash:salt:)")));
- (void)markAuditSyncedId:(int64_t)id __attribute__((swift_name("markAuditSynced(id:)")));
- (void)purgeOldDiagnosticsGenerated_at:(int64_t)generated_at __attribute__((swift_name("purgeOldDiagnostics(generated_at:)")));
- (void)purgeOldFleetHealthTimestamp:(int64_t)timestamp __attribute__((swift_name("purgeOldFleetHealth(timestamp:)")));
- (void)purgeOldHistoryTimestamp:(int64_t)timestamp __attribute__((swift_name("purgeOldHistory(timestamp:)")));
- (void)purgeOldMlScoresTimestamp:(int64_t)timestamp __attribute__((swift_name("purgeOldMlScores(timestamp:)")));
- (void)upsertDiagnosticBattery_index:(int64_t)battery_index battery_index_:(int64_t)battery_index_ diagnostic_text:(NSString *)diagnostic_text severity:(NSString *)severity generated_at:(int64_t)generated_at __attribute__((swift_name("upsertDiagnostic(battery_index:battery_index_:diagnostic_text:severity:generated_at:)")));
- (void)upsertFleetHealthFleet_health_score:(double)fleet_health_score outlier_idx:(int64_t)outlier_idx outlier_score:(double)outlier_score imbalance_severity:(double)imbalance_severity timestamp:(int64_t)timestamp __attribute__((swift_name("upsertFleetHealth(fleet_health_score:outlier_idx:outlier_score:imbalance_severity:timestamp:)")));
- (void)upsertMlScoreBattery_index:(int64_t)battery_index battery_index_:(int64_t)battery_index_ soh_score:(double)soh_score rul_days:(int64_t)rul_days anomaly_score:(double)anomaly_score r_int_trend:(double)r_int_trend timestamp:(int64_t)timestamp __attribute__((swift_name("upsertMlScore(battery_index:battery_index_:soh_score:rul_days:anomaly_score:r_int_trend:timestamp:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("DatabaseHelper")))
@interface SharedDatabaseHelper : SharedBase
- (instancetype)initWithDriverFactory:(SharedDriverFactory *)driverFactory __attribute__((swift_name("init(driverFactory:)"))) __attribute__((objc_designated_initializer));
- (int64_t)countPendingSync __attribute__((swift_name("countPendingSync()")));
- (int64_t)countUnsyncedAudit __attribute__((swift_name("countUnsyncedAudit()")));
- (int64_t)countUsers __attribute__((swift_name("countUsers()")));
- (void)deleteUserUserId:(NSString *)userId __attribute__((swift_name("deleteUser(userId:)")));
- (SharedUserProfile * _Nullable)findUserByHashPinHash:(NSString *)pinHash __attribute__((swift_name("findUserByHash(pinHash:)")));
- (NSArray<SharedDiagnostic *> *)getAllDiagnostics __attribute__((swift_name("getAllDiagnostics()")));
- (NSArray<SharedUserProfile *> *)getAllUsers __attribute__((swift_name("getAllUsers()")));
- (NSArray<SharedAuditEvent *> *)getAuditEventsLimit:(int32_t)limit __attribute__((swift_name("getAuditEvents(limit:)")));
- (NSArray<SharedAuditEvent *> *)getAuditFilteredAction:(NSString * _Nullable)action batteryIndex:(SharedInt * _Nullable)batteryIndex limit:(int32_t)limit __attribute__((swift_name("getAuditFiltered(action:batteryIndex:limit:)")));
- (SharedDiagnostic * _Nullable)getDiagnosticBatteryIndex:(int32_t)batteryIndex __attribute__((swift_name("getDiagnostic(batteryIndex:)")));
- (NSArray<SharedBatteryHistoryPoint *> *)getHistoryBatteryIndex:(int32_t)batteryIndex sinceMs:(int64_t)sinceMs __attribute__((swift_name("getHistory(batteryIndex:sinceMs:)")));
- (NSArray<SharedBatteryState *> *)getLastKnownBatteries __attribute__((swift_name("getLastKnownBatteries()")));
- (SharedFleetHealth * _Nullable)getLatestFleetHealth __attribute__((swift_name("getLatestFleetHealth()")));
- (NSArray<SharedMlScore *> *)getLatestMlScores __attribute__((swift_name("getLatestMlScores()")));
- (SharedMlScore * _Nullable)getMlScoreBatteryIndex:(int32_t)batteryIndex __attribute__((swift_name("getMlScore(batteryIndex:)")));
- (NSArray<SharedPendingAuditSyncItem *> *)getUnsyncedAuditLimit:(int32_t)limit __attribute__((swift_name("getUnsyncedAudit(limit:)")));
- (void)insertAuditEvent:(SharedAuditEvent *)event __attribute__((swift_name("insertAudit(event:)")));
- (void)insertHistoryBattery:(SharedBatteryState *)battery __attribute__((swift_name("insertHistory(battery:)")));
- (void)insertUserUser:(SharedUserProfile *)user __attribute__((swift_name("insertUser(user:)")));
- (void)markAuditSyncedIds:(NSArray<SharedLong *> *)ids __attribute__((swift_name("markAuditSynced(ids:)")));
- (void)purgeOldHistoryOlderThanMs:(int64_t)olderThanMs __attribute__((swift_name("purgeOldHistory(olderThanMs:)")));
- (void)upsertDiagnosticDiag:(SharedDiagnostic *)diag __attribute__((swift_name("upsertDiagnostic(diag:)")));
- (void)upsertFleetHealthFleet:(SharedFleetHealth *)fleet __attribute__((swift_name("upsertFleetHealth(fleet:)")));
- (void)upsertMlScoreScore:(SharedMlScore *)score __attribute__((swift_name("upsertMlScore(score:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Diagnostics")))
@interface SharedDiagnostics : SharedBase
- (instancetype)initWithId:(int64_t)id battery_index:(int64_t)battery_index diagnostic_text:(NSString *)diagnostic_text severity:(NSString *)severity generated_at:(int64_t)generated_at __attribute__((swift_name("init(id:battery_index:diagnostic_text:severity:generated_at:)"))) __attribute__((objc_designated_initializer));
- (SharedDiagnostics *)doCopyId:(int64_t)id battery_index:(int64_t)battery_index diagnostic_text:(NSString *)diagnostic_text severity:(NSString *)severity generated_at:(int64_t)generated_at __attribute__((swift_name("doCopy(id:battery_index:diagnostic_text:severity:generated_at:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int64_t battery_index __attribute__((swift_name("battery_index")));
@property (readonly) NSString *diagnostic_text __attribute__((swift_name("diagnostic_text")));
@property (readonly) int64_t generated_at __attribute__((swift_name("generated_at")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) NSString *severity __attribute__((swift_name("severity")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("DriverFactory")))
@interface SharedDriverFactory : SharedBase
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (id<SharedRuntimeSqlDriver>)createDriver __attribute__((swift_name("createDriver()")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Fleet_health")))
@interface SharedFleet_health : SharedBase
- (instancetype)initWithId:(int64_t)id fleet_health_score:(double)fleet_health_score outlier_idx:(int64_t)outlier_idx outlier_score:(double)outlier_score imbalance_severity:(double)imbalance_severity timestamp:(int64_t)timestamp __attribute__((swift_name("init(id:fleet_health_score:outlier_idx:outlier_score:imbalance_severity:timestamp:)"))) __attribute__((objc_designated_initializer));
- (SharedFleet_health *)doCopyId:(int64_t)id fleet_health_score:(double)fleet_health_score outlier_idx:(int64_t)outlier_idx outlier_score:(double)outlier_score imbalance_severity:(double)imbalance_severity timestamp:(int64_t)timestamp __attribute__((swift_name("doCopy(id:fleet_health_score:outlier_idx:outlier_score:imbalance_severity:timestamp:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) double fleet_health_score __attribute__((swift_name("fleet_health_score")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) double imbalance_severity __attribute__((swift_name("imbalance_severity")));
@property (readonly) int64_t outlier_idx __attribute__((swift_name("outlier_idx")));
@property (readonly) double outlier_score __attribute__((swift_name("outlier_score")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Ml_scores")))
@interface SharedMl_scores : SharedBase
- (instancetype)initWithId:(int64_t)id battery_index:(int64_t)battery_index soh_score:(double)soh_score rul_days:(int64_t)rul_days anomaly_score:(double)anomaly_score r_int_trend:(double)r_int_trend timestamp:(int64_t)timestamp __attribute__((swift_name("init(id:battery_index:soh_score:rul_days:anomaly_score:r_int_trend:timestamp:)"))) __attribute__((objc_designated_initializer));
- (SharedMl_scores *)doCopyId:(int64_t)id battery_index:(int64_t)battery_index soh_score:(double)soh_score rul_days:(int64_t)rul_days anomaly_score:(double)anomaly_score r_int_trend:(double)r_int_trend timestamp:(int64_t)timestamp __attribute__((swift_name("doCopy(id:battery_index:soh_score:rul_days:anomaly_score:r_int_trend:timestamp:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) double anomaly_score __attribute__((swift_name("anomaly_score")));
@property (readonly) int64_t battery_index __attribute__((swift_name("battery_index")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) double r_int_trend __attribute__((swift_name("r_int_trend")));
@property (readonly) int64_t rul_days __attribute__((swift_name("rul_days")));
@property (readonly) double soh_score __attribute__((swift_name("soh_score")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("PendingAuditSyncItem")))
@interface SharedPendingAuditSyncItem : SharedBase
- (instancetype)initWithId:(int64_t)id event:(SharedAuditEvent *)event __attribute__((swift_name("init(id:event:)"))) __attribute__((objc_designated_initializer));
- (SharedPendingAuditSyncItem *)doCopyId:(int64_t)id event:(SharedAuditEvent *)event __attribute__((swift_name("doCopy(id:event:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) SharedAuditEvent *event __attribute__((swift_name("event")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Sync_queue")))
@interface SharedSync_queue : SharedBase
- (instancetype)initWithId:(int64_t)id type:(NSString *)type payload:(NSString *)payload created_at:(int64_t)created_at retry_count:(int64_t)retry_count __attribute__((swift_name("init(id:type:payload:created_at:retry_count:)"))) __attribute__((objc_designated_initializer));
- (SharedSync_queue *)doCopyId:(int64_t)id type:(NSString *)type payload:(NSString *)payload created_at:(int64_t)created_at retry_count:(int64_t)retry_count __attribute__((swift_name("doCopy(id:type:payload:created_at:retry_count:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int64_t created_at __attribute__((swift_name("created_at")));
@property (readonly) int64_t id __attribute__((swift_name("id")));
@property (readonly) NSString *payload __attribute__((swift_name("payload")));
@property (readonly) int64_t retry_count __attribute__((swift_name("retry_count")));
@property (readonly) NSString *type __attribute__((swift_name("type")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("User_profiles")))
@interface SharedUser_profiles : SharedBase
- (instancetype)initWithId:(NSString *)id name:(NSString *)name role:(NSString *)role pin_hash:(NSString *)pin_hash salt:(NSString *)salt __attribute__((swift_name("init(id:name:role:pin_hash:salt:)"))) __attribute__((objc_designated_initializer));
- (SharedUser_profiles *)doCopyId:(NSString *)id name:(NSString *)name role:(NSString *)role pin_hash:(NSString *)pin_hash salt:(NSString *)salt __attribute__((swift_name("doCopy(id:name:role:pin_hash:salt:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *id __attribute__((swift_name("id")));
@property (readonly) NSString *name __attribute__((swift_name("name")));
@property (readonly) NSString *pin_hash __attribute__((swift_name("pin_hash")));
@property (readonly) NSString *role __attribute__((swift_name("role")));
@property (readonly) NSString *salt __attribute__((swift_name("salt")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("ConfigUseCase")))
@interface SharedConfigUseCase : SharedBase
- (instancetype)initWithTransport:(SharedTransportManager *)transport recordAudit:(void (^)(NSString *, SharedInt * _Nullable, NSString * _Nullable))recordAudit __attribute__((swift_name("init(transport:recordAudit:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithTransport:(SharedTransportManager *)transport audit:(SharedAuditUseCase *)audit currentUserId:(NSString *(^)(void))currentUserId __attribute__((swift_name("init(transport:audit:currentUserId:)"))) __attribute__((objc_designated_initializer));
- (void)close __attribute__((swift_name("close()")));
- (SharedProtectionConfig *)getCurrentConfig __attribute__((swift_name("getCurrentConfig()")));
- (int64_t)getPendingSyncCount __attribute__((swift_name("getPendingSyncCount()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigMinMv:(int32_t)minMv maxMv:(int32_t)maxMv maxMa:(int32_t)maxMa diffMv:(int32_t)diffMv completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(minMv:maxMv:maxMa:diffMv:completionHandler:)")));
- (void)setProtectionConfigMinMv:(int32_t)minMv maxMv:(int32_t)maxMv maxMa:(int32_t)maxMa diffMv:(int32_t)diffMv callback:(void (^)(SharedCommandResult *))callback __attribute__((swift_name("setProtectionConfig(minMv:maxMv:maxMa:diffMv:callback:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password callback:(void (^)(SharedCommandResult *))callback __attribute__((swift_name("setWifiConfig(ssid:password:callback:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("ControlUseCase")))
@interface SharedControlUseCase : SharedBase
- (instancetype)initWithTransport:(SharedTransportManager *)transport recordAudit:(void (^)(NSString *, SharedInt * _Nullable, NSString * _Nullable))recordAudit __attribute__((swift_name("init(transport:recordAudit:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithTransport:(SharedTransportManager *)transport audit:(SharedAuditUseCase *)audit currentUserId:(NSString *(^)(void))currentUserId __attribute__((swift_name("init(transport:audit:currentUserId:)"))) __attribute__((objc_designated_initializer));
- (void)close __attribute__((swift_name("close()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));
- (void)resetSwitchCountIndex:(int32_t)index callback:(void (^)(SharedCommandResult *))callback __attribute__((swift_name("resetSwitchCount(index:callback:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on callback:(void (^)(SharedCommandResult *))callback __attribute__((swift_name("switchBattery(index:on:callback:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("MonitoringUseCase")))
@interface SharedMonitoringUseCase : SharedBase
- (instancetype)initWithTransport:(SharedTransportManager *)transport db:(SharedDatabaseHelper *)db __attribute__((swift_name("init(transport:db:)"))) __attribute__((objc_designated_initializer));
- (void)close __attribute__((swift_name("close()")));
- (NSArray<SharedBatteryHistoryPoint *> *)getHistoryBatteryIndex:(int32_t)batteryIndex hours:(int32_t)hours __attribute__((swift_name("getHistory(batteryIndex:hours:)")));
- (void)getHistoryBatteryIndex:(int32_t)batteryIndex hours:(int32_t)hours callback:(void (^)(NSArray<SharedBatteryHistoryPoint *> *))callback __attribute__((swift_name("getHistory(batteryIndex:hours:callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (void)observeBatteriesCallback:(void (^)(NSArray<SharedBatteryState *> *))callback __attribute__((swift_name("observeBatteries(callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteryIndex:(int32_t)index __attribute__((swift_name("observeBattery(index:)")));
- (void)observeBatteryIndex:(int32_t)index callback:(void (^)(SharedBatteryState * _Nullable))callback __attribute__((swift_name("observeBattery(index:callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeHealth __attribute__((swift_name("observeHealth()")));
- (void)observeHealthCallback:(void (^)(NSArray<SharedBatteryHealth *> *))callback __attribute__((swift_name("observeHealth(callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeHealthIndex:(int32_t)index __attribute__((swift_name("observeHealth(index:)")));
- (void)observeHealthIndex:(int32_t)index callback:(void (^)(SharedBatteryHealth * _Nullable))callback __attribute__((swift_name("observeHealth(index:callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (void)observeSolarCallback:(void (^)(SharedSolarData * _Nullable))callback __attribute__((swift_name("observeSolar(callback:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));
- (void)observeSystemCallback:(void (^)(SharedSystemInfo * _Nullable))callback __attribute__((swift_name("observeSystem(callback:)")));
- (void)startRecording __attribute__((swift_name("startRecording()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)triggerRintMeasurementBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("triggerRintMeasurement(batteryIndex:completionHandler:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SohUseCase")))
@interface SharedSohUseCase : SharedBase
- (instancetype)initWithRestClient:(SharedSohRestClient *)restClient db:(SharedDatabaseHelper *)db __attribute__((swift_name("init(restClient:db:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedSohUseCaseCompanion *companion __attribute__((swift_name("companion")));
- (void)close __attribute__((swift_name("close()")));
- (NSArray<SharedMlScore *> *)getAnomalyAlerts __attribute__((swift_name("getAnomalyAlerts()")));
- (SharedDiagnostic * _Nullable)getCachedDiagnosticBatteryIndex:(int32_t)batteryIndex __attribute__((swift_name("getCachedDiagnostic(batteryIndex:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getDiagnosticBatteryIndex:(int32_t)batteryIndex forceRefresh:(BOOL)forceRefresh completionHandler:(void (^)(SharedDiagnostic * _Nullable_result, NSError * _Nullable))completionHandler __attribute__((swift_name("getDiagnostic(batteryIndex:forceRefresh:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getSohHistoryBatteryIndex:(int32_t)batteryIndex days:(int32_t)days completionHandler:(void (^)(NSArray<SharedMlScore *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getSohHistory(batteryIndex:days:completionHandler:)")));
- (void)observeFleetHealthCallback:(void (^)(SharedFleetHealth * _Nullable))callback __attribute__((swift_name("observeFleetHealth(callback:)")));
- (void)observeMlScoresCallback:(void (^)(NSArray<SharedMlScore *> *))callback __attribute__((swift_name("observeMlScores(callback:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)refreshFromCloudWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("refreshFromCloud(completionHandler:)")));
- (void)start __attribute__((swift_name("start()")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> fleetHealth __attribute__((swift_name("fleetHealth")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isRefreshing __attribute__((swift_name("isRefreshing")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> lastRefreshMs __attribute__((swift_name("lastRefreshMs")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> mlScores __attribute__((swift_name("mlScores")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SohUseCase.Companion")))
@interface SharedSohUseCaseCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedSohUseCaseCompanion *shared __attribute__((swift_name("shared")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("AuditEvent")))
@interface SharedAuditEvent : SharedBase
- (instancetype)initWithTimestamp:(int64_t)timestamp userId:(NSString *)userId action:(NSString *)action target:(SharedInt * _Nullable)target detail:(NSString * _Nullable)detail __attribute__((swift_name("init(timestamp:userId:action:target:detail:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedAuditEventCompanion *companion __attribute__((swift_name("companion")));
- (SharedAuditEvent *)doCopyTimestamp:(int64_t)timestamp userId:(NSString *)userId action:(NSString *)action target:(SharedInt * _Nullable)target detail:(NSString * _Nullable)detail __attribute__((swift_name("doCopy(timestamp:userId:action:target:detail:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *action __attribute__((swift_name("action")));
@property (readonly) NSString * _Nullable detail __attribute__((swift_name("detail")));
@property (readonly) SharedInt * _Nullable target __attribute__((swift_name("target")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@property (readonly) NSString *userId __attribute__((swift_name("userId")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("AuditEvent.Companion")))
@interface SharedAuditEventCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedAuditEventCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryHealth")))
@interface SharedBatteryHealth : SharedBase
- (instancetype)initWithIndex:(int32_t)index sohPercent:(int32_t)sohPercent rOhmicMohm:(float)rOhmicMohm rTotalMohm:(float)rTotalMohm rintValid:(BOOL)rintValid sohConfidence:(int32_t)sohConfidence timestamp:(int64_t)timestamp __attribute__((swift_name("init(index:sohPercent:rOhmicMohm:rTotalMohm:rintValid:sohConfidence:timestamp:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedBatteryHealthCompanion *companion __attribute__((swift_name("companion")));
- (SharedBatteryHealth *)doCopyIndex:(int32_t)index sohPercent:(int32_t)sohPercent rOhmicMohm:(float)rOhmicMohm rTotalMohm:(float)rTotalMohm rintValid:(BOOL)rintValid sohConfidence:(int32_t)sohConfidence timestamp:(int64_t)timestamp __attribute__((swift_name("doCopy(index:sohPercent:rOhmicMohm:rTotalMohm:rintValid:sohConfidence:timestamp:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t index __attribute__((swift_name("index")));
@property (readonly) float rOhmicMohm __attribute__((swift_name("rOhmicMohm")));
@property (readonly) float rTotalMohm __attribute__((swift_name("rTotalMohm")));
@property (readonly) BOOL rintValid __attribute__((swift_name("rintValid")));
@property (readonly) int32_t sohConfidence __attribute__((swift_name("sohConfidence")));
@property (readonly) int32_t sohPercent __attribute__((swift_name("sohPercent")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryHealth.Companion")))
@interface SharedBatteryHealthCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedBatteryHealthCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryState")))
@interface SharedBatteryState : SharedBase
- (instancetype)initWithIndex:(int32_t)index voltageMv:(int32_t)voltageMv currentMa:(int32_t)currentMa state:(SharedBatteryStatus *)state ahDischargeMah:(int32_t)ahDischargeMah ahChargeMah:(int32_t)ahChargeMah nbSwitch:(int32_t)nbSwitch __attribute__((swift_name("init(index:voltageMv:currentMa:state:ahDischargeMah:ahChargeMah:nbSwitch:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedBatteryStateCompanion *companion __attribute__((swift_name("companion")));
- (SharedBatteryState *)doCopyIndex:(int32_t)index voltageMv:(int32_t)voltageMv currentMa:(int32_t)currentMa state:(SharedBatteryStatus *)state ahDischargeMah:(int32_t)ahDischargeMah ahChargeMah:(int32_t)ahChargeMah nbSwitch:(int32_t)nbSwitch __attribute__((swift_name("doCopy(index:voltageMv:currentMa:state:ahDischargeMah:ahChargeMah:nbSwitch:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t ahChargeMah __attribute__((swift_name("ahChargeMah")));
@property (readonly) int32_t ahDischargeMah __attribute__((swift_name("ahDischargeMah")));
@property (readonly) int32_t currentMa __attribute__((swift_name("currentMa")));
@property (readonly) int32_t index __attribute__((swift_name("index")));
@property (readonly) int32_t nbSwitch __attribute__((swift_name("nbSwitch")));
@property (readonly) SharedBatteryStatus *state __attribute__((swift_name("state")));
@property (readonly) int32_t voltageMv __attribute__((swift_name("voltageMv")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryState.Companion")))
@interface SharedBatteryStateCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedBatteryStateCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end

__attribute__((swift_name("KotlinComparable")))
@protocol SharedKotlinComparable
@required
- (int32_t)compareToOther:(id _Nullable)other __attribute__((swift_name("compareTo(other:)")));
@end

__attribute__((swift_name("KotlinEnum")))
@interface SharedKotlinEnum<E> : SharedBase <SharedKotlinComparable>
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedKotlinEnumCompanion *companion __attribute__((swift_name("companion")));
- (int32_t)compareToOther:(E)other __attribute__((swift_name("compareTo(other:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *name __attribute__((swift_name("name")));
@property (readonly) int32_t ordinal __attribute__((swift_name("ordinal")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryStatus")))
@interface SharedBatteryStatus : SharedKotlinEnum<SharedBatteryStatus *>
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer)) __attribute__((unavailable));
@property (class, readonly, getter=companion) SharedBatteryStatusCompanion *companion __attribute__((swift_name("companion")));
@property (class, readonly) SharedBatteryStatus *connected __attribute__((swift_name("connected")));
@property (class, readonly) SharedBatteryStatus *disconnected __attribute__((swift_name("disconnected")));
@property (class, readonly) SharedBatteryStatus *reconnecting __attribute__((swift_name("reconnecting")));
@property (class, readonly) SharedBatteryStatus *error __attribute__((swift_name("error")));
@property (class, readonly) SharedBatteryStatus *locked __attribute__((swift_name("locked")));
+ (SharedKotlinArray<SharedBatteryStatus *> *)values __attribute__((swift_name("values()")));
@property (class, readonly) NSArray<SharedBatteryStatus *> *entries __attribute__((swift_name("entries")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BatteryStatus.Companion")))
@interface SharedBatteryStatusCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedBatteryStatusCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializerTypeParamsSerializers:(SharedKotlinArray<id<SharedKotlinx_serialization_coreKSerializer>> *)typeParamsSerializers __attribute__((swift_name("serializer(typeParamsSerializers:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("CommandResult")))
@interface SharedCommandResult : SharedBase
- (instancetype)initWithIsSuccess:(BOOL)isSuccess errorMessage:(NSString * _Nullable)errorMessage __attribute__((swift_name("init(isSuccess:errorMessage:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedCommandResultCompanion *companion __attribute__((swift_name("companion")));
- (SharedCommandResult *)doCopyIsSuccess:(BOOL)isSuccess errorMessage:(NSString * _Nullable)errorMessage __attribute__((swift_name("doCopy(isSuccess:errorMessage:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString * _Nullable errorMessage __attribute__((swift_name("errorMessage")));
@property (readonly) BOOL isSuccess __attribute__((swift_name("isSuccess")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("CommandResult.Companion")))
@interface SharedCommandResultCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedCommandResultCompanion *shared __attribute__((swift_name("shared")));
- (SharedCommandResult *)errorMsg:(NSString *)msg __attribute__((swift_name("error(msg:)")));
- (SharedCommandResult *)ok __attribute__((swift_name("ok()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Diagnostic")))
@interface SharedDiagnostic : SharedBase
- (instancetype)initWithBattery:(int32_t)battery diagnostic:(NSString *)diagnostic severity:(SharedDiagnosticSeverity *)severity generatedAt:(int64_t)generatedAt __attribute__((swift_name("init(battery:diagnostic:severity:generatedAt:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedDiagnosticCompanion *companion __attribute__((swift_name("companion")));
- (SharedDiagnostic *)doCopyBattery:(int32_t)battery diagnostic:(NSString *)diagnostic severity:(SharedDiagnosticSeverity *)severity generatedAt:(int64_t)generatedAt __attribute__((swift_name("doCopy(battery:diagnostic:severity:generatedAt:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t battery __attribute__((swift_name("battery")));
@property (readonly) NSString *diagnostic __attribute__((swift_name("diagnostic")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="generated_at")
*/
@property (readonly) int64_t generatedAt __attribute__((swift_name("generatedAt")));
@property (readonly) SharedDiagnosticSeverity *severity __attribute__((swift_name("severity")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("Diagnostic.Companion")))
@interface SharedDiagnosticCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedDiagnosticCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("DiagnosticSeverity")))
@interface SharedDiagnosticSeverity : SharedKotlinEnum<SharedDiagnosticSeverity *>
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer)) __attribute__((unavailable));
@property (class, readonly, getter=companion) SharedDiagnosticSeverityCompanion *companion __attribute__((swift_name("companion")));
@property (class, readonly) SharedDiagnosticSeverity *info __attribute__((swift_name("info")));
@property (class, readonly) SharedDiagnosticSeverity *warning __attribute__((swift_name("warning")));
@property (class, readonly) SharedDiagnosticSeverity *critical __attribute__((swift_name("critical")));
+ (SharedKotlinArray<SharedDiagnosticSeverity *> *)values __attribute__((swift_name("values()")));
@property (class, readonly) NSArray<SharedDiagnosticSeverity *> *entries __attribute__((swift_name("entries")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("DiagnosticSeverity.Companion")))
@interface SharedDiagnosticSeverityCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedDiagnosticSeverityCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializerTypeParamsSerializers:(SharedKotlinArray<id<SharedKotlinx_serialization_coreKSerializer>> *)typeParamsSerializers __attribute__((swift_name("serializer(typeParamsSerializers:)")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("FleetDiagnostic")))
@interface SharedFleetDiagnostic : SharedBase
- (instancetype)initWithSummary:(NSString *)summary severity:(SharedDiagnosticSeverity *)severity generatedAt:(int64_t)generatedAt __attribute__((swift_name("init(summary:severity:generatedAt:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedFleetDiagnosticCompanion *companion __attribute__((swift_name("companion")));
- (SharedFleetDiagnostic *)doCopySummary:(NSString *)summary severity:(SharedDiagnosticSeverity *)severity generatedAt:(int64_t)generatedAt __attribute__((swift_name("doCopy(summary:severity:generatedAt:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="generated_at")
*/
@property (readonly) int64_t generatedAt __attribute__((swift_name("generatedAt")));
@property (readonly) SharedDiagnosticSeverity *severity __attribute__((swift_name("severity")));
@property (readonly) NSString *summary __attribute__((swift_name("summary")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("FleetDiagnostic.Companion")))
@interface SharedFleetDiagnosticCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedFleetDiagnosticCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("FleetHealth")))
@interface SharedFleetHealth : SharedBase
- (instancetype)initWithFleetHealth:(float)fleetHealth outlierIdx:(int32_t)outlierIdx outlierScore:(float)outlierScore imbalanceSeverity:(float)imbalanceSeverity timestamp:(int64_t)timestamp __attribute__((swift_name("init(fleetHealth:outlierIdx:outlierScore:imbalanceSeverity:timestamp:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedFleetHealthCompanion *companion __attribute__((swift_name("companion")));
- (SharedFleetHealth *)doCopyFleetHealth:(float)fleetHealth outlierIdx:(int32_t)outlierIdx outlierScore:(float)outlierScore imbalanceSeverity:(float)imbalanceSeverity timestamp:(int64_t)timestamp __attribute__((swift_name("doCopy(fleetHealth:outlierIdx:outlierScore:imbalanceSeverity:timestamp:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="fleet_health")
*/
@property (readonly) float fleetHealth __attribute__((swift_name("fleetHealth")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="imbalance_severity")
*/
@property (readonly) float imbalanceSeverity __attribute__((swift_name("imbalanceSeverity")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="outlier_idx")
*/
@property (readonly) int32_t outlierIdx __attribute__((swift_name("outlierIdx")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="outlier_score")
*/
@property (readonly) float outlierScore __attribute__((swift_name("outlierScore")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("FleetHealth.Companion")))
@interface SharedFleetHealthCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedFleetHealthCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("MlScore")))
@interface SharedMlScore : SharedBase
- (instancetype)initWithBattery:(int32_t)battery sohScore:(float)sohScore rulDays:(int32_t)rulDays anomalyScore:(float)anomalyScore rIntTrendMohmPerDay:(float)rIntTrendMohmPerDay timestamp:(int64_t)timestamp __attribute__((swift_name("init(battery:sohScore:rulDays:anomalyScore:rIntTrendMohmPerDay:timestamp:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedMlScoreCompanion *companion __attribute__((swift_name("companion")));
- (SharedMlScore *)doCopyBattery:(int32_t)battery sohScore:(float)sohScore rulDays:(int32_t)rulDays anomalyScore:(float)anomalyScore rIntTrendMohmPerDay:(float)rIntTrendMohmPerDay timestamp:(int64_t)timestamp __attribute__((swift_name("doCopy(battery:sohScore:rulDays:anomalyScore:rIntTrendMohmPerDay:timestamp:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="anomaly_score")
*/
@property (readonly) float anomalyScore __attribute__((swift_name("anomalyScore")));
@property (readonly) int32_t battery __attribute__((swift_name("battery")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="r_int_trend_mohm_per_day")
*/
@property (readonly) float rIntTrendMohmPerDay __attribute__((swift_name("rIntTrendMohmPerDay")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="rul_days")
*/
@property (readonly) int32_t rulDays __attribute__((swift_name("rulDays")));

/**
 * @note annotations
 *   kotlinx.serialization.SerialName(value="soh_score")
*/
@property (readonly) float sohScore __attribute__((swift_name("sohScore")));
@property (readonly) int64_t timestamp __attribute__((swift_name("timestamp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("MlScore.Companion")))
@interface SharedMlScoreCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedMlScoreCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("ProtectionConfig")))
@interface SharedProtectionConfig : SharedBase
- (instancetype)initWithMinMv:(int32_t)minMv maxMv:(int32_t)maxMv maxMa:(int32_t)maxMa diffMv:(int32_t)diffMv __attribute__((swift_name("init(minMv:maxMv:maxMa:diffMv:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedProtectionConfigCompanion *companion __attribute__((swift_name("companion")));
- (SharedProtectionConfig *)doCopyMinMv:(int32_t)minMv maxMv:(int32_t)maxMv maxMa:(int32_t)maxMa diffMv:(int32_t)diffMv __attribute__((swift_name("doCopy(minMv:maxMv:maxMa:diffMv:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t diffMv __attribute__((swift_name("diffMv")));
@property (readonly) int32_t maxMa __attribute__((swift_name("maxMa")));
@property (readonly) int32_t maxMv __attribute__((swift_name("maxMv")));
@property (readonly) int32_t minMv __attribute__((swift_name("minMv")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("ProtectionConfig.Companion")))
@interface SharedProtectionConfigCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedProtectionConfigCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SolarData")))
@interface SharedSolarData : SharedBase
- (instancetype)initWithBatteryVoltageMv:(int32_t)batteryVoltageMv batteryCurrentMa:(int32_t)batteryCurrentMa panelVoltageMv:(int32_t)panelVoltageMv panelPowerW:(int32_t)panelPowerW chargeState:(int32_t)chargeState chargeStateName:(NSString *)chargeStateName yieldTodayWh:(int64_t)yieldTodayWh valid:(BOOL)valid __attribute__((swift_name("init(batteryVoltageMv:batteryCurrentMa:panelVoltageMv:panelPowerW:chargeState:chargeStateName:yieldTodayWh:valid:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedSolarDataCompanion *companion __attribute__((swift_name("companion")));
- (SharedSolarData *)doCopyBatteryVoltageMv:(int32_t)batteryVoltageMv batteryCurrentMa:(int32_t)batteryCurrentMa panelVoltageMv:(int32_t)panelVoltageMv panelPowerW:(int32_t)panelPowerW chargeState:(int32_t)chargeState chargeStateName:(NSString *)chargeStateName yieldTodayWh:(int64_t)yieldTodayWh valid:(BOOL)valid __attribute__((swift_name("doCopy(batteryVoltageMv:batteryCurrentMa:panelVoltageMv:panelPowerW:chargeState:chargeStateName:yieldTodayWh:valid:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t batteryCurrentMa __attribute__((swift_name("batteryCurrentMa")));
@property (readonly) int32_t batteryVoltageMv __attribute__((swift_name("batteryVoltageMv")));
@property (readonly) int32_t chargeState __attribute__((swift_name("chargeState")));
@property (readonly) NSString *chargeStateName __attribute__((swift_name("chargeStateName")));
@property (readonly) int32_t panelPowerW __attribute__((swift_name("panelPowerW")));
@property (readonly) int32_t panelVoltageMv __attribute__((swift_name("panelVoltageMv")));
@property (readonly) BOOL valid __attribute__((swift_name("valid")));
@property (readonly) int64_t yieldTodayWh __attribute__((swift_name("yieldTodayWh")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SolarData.Companion")))
@interface SharedSolarDataCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedSolarDataCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SystemInfo")))
@interface SharedSystemInfo : SharedBase
- (instancetype)initWithFirmwareVersion:(NSString *)firmwareVersion heapFree:(int64_t)heapFree uptimeSeconds:(int64_t)uptimeSeconds wifiIp:(NSString * _Nullable)wifiIp nbIna:(int32_t)nbIna nbTca:(int32_t)nbTca topologyValid:(BOOL)topologyValid __attribute__((swift_name("init(firmwareVersion:heapFree:uptimeSeconds:wifiIp:nbIna:nbTca:topologyValid:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedSystemInfoCompanion *companion __attribute__((swift_name("companion")));
- (SharedSystemInfo *)doCopyFirmwareVersion:(NSString *)firmwareVersion heapFree:(int64_t)heapFree uptimeSeconds:(int64_t)uptimeSeconds wifiIp:(NSString * _Nullable)wifiIp nbIna:(int32_t)nbIna nbTca:(int32_t)nbTca topologyValid:(BOOL)topologyValid __attribute__((swift_name("doCopy(firmwareVersion:heapFree:uptimeSeconds:wifiIp:nbIna:nbTca:topologyValid:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *firmwareVersion __attribute__((swift_name("firmwareVersion")));
@property (readonly) int64_t heapFree __attribute__((swift_name("heapFree")));
@property (readonly) int32_t nbIna __attribute__((swift_name("nbIna")));
@property (readonly) int32_t nbTca __attribute__((swift_name("nbTca")));
@property (readonly) BOOL topologyValid __attribute__((swift_name("topologyValid")));
@property (readonly) int64_t uptimeSeconds __attribute__((swift_name("uptimeSeconds")));
@property (readonly) NSString * _Nullable wifiIp __attribute__((swift_name("wifiIp")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SystemInfo.Companion")))
@interface SharedSystemInfoCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedSystemInfoCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("TransportChannel")))
@interface SharedTransportChannel : SharedKotlinEnum<SharedTransportChannel *>
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer)) __attribute__((unavailable));
@property (class, readonly) SharedTransportChannel *ble __attribute__((swift_name("ble")));
@property (class, readonly) SharedTransportChannel *wifi __attribute__((swift_name("wifi")));
@property (class, readonly) SharedTransportChannel *mqttCloud __attribute__((swift_name("mqttCloud")));
@property (class, readonly) SharedTransportChannel *restCloud __attribute__((swift_name("restCloud")));
@property (class, readonly) SharedTransportChannel *offline __attribute__((swift_name("offline")));
+ (SharedKotlinArray<SharedTransportChannel *> *)values __attribute__((swift_name("values()")));
@property (class, readonly) NSArray<SharedTransportChannel *> *entries __attribute__((swift_name("entries")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("UserProfile")))
@interface SharedUserProfile : SharedBase
- (instancetype)initWithId:(NSString *)id name:(NSString *)name role:(SharedUserRole *)role pinHash:(NSString *)pinHash salt:(NSString *)salt __attribute__((swift_name("init(id:name:role:pinHash:salt:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedUserProfileCompanion *companion __attribute__((swift_name("companion")));
- (SharedUserProfile *)doCopyId:(NSString *)id name:(NSString *)name role:(SharedUserRole *)role pinHash:(NSString *)pinHash salt:(NSString *)salt __attribute__((swift_name("doCopy(id:name:role:pinHash:salt:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) NSString *id __attribute__((swift_name("id")));
@property (readonly) NSString *name __attribute__((swift_name("name")));
@property (readonly) NSString *pinHash __attribute__((swift_name("pinHash")));
@property (readonly) SharedUserRole *role __attribute__((swift_name("role")));
@property (readonly) NSString *salt __attribute__((swift_name("salt")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("UserProfile.Companion")))
@interface SharedUserProfileCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedUserProfileCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("UserRole")))
@interface SharedUserRole : SharedKotlinEnum<SharedUserRole *>
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer)) __attribute__((unavailable));
@property (class, readonly, getter=companion) SharedUserRoleCompanion *companion __attribute__((swift_name("companion")));
@property (class, readonly) SharedUserRole *admin __attribute__((swift_name("admin")));
@property (class, readonly) SharedUserRole *technician __attribute__((swift_name("technician")));
@property (class, readonly) SharedUserRole *viewer __attribute__((swift_name("viewer")));
+ (SharedKotlinArray<SharedUserRole *> *)values __attribute__((swift_name("values()")));
@property (class, readonly) NSArray<SharedUserRole *> *entries __attribute__((swift_name("entries")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("UserRole.Companion")))
@interface SharedUserRoleCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedUserRoleCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializerTypeParamsSerializers:(SharedKotlinArray<id<SharedKotlinx_serialization_coreKSerializer>> *)typeParamsSerializers __attribute__((swift_name("serializer(typeParamsSerializers:)")));
@end


/**
 * @note annotations
 *   kotlinx.serialization.Serializable
*/
__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("WifiStatusInfo")))
@interface SharedWifiStatusInfo : SharedBase
- (instancetype)initWithSsid:(NSString *)ssid ip:(NSString *)ip rssi:(int32_t)rssi connected:(BOOL)connected __attribute__((swift_name("init(ssid:ip:rssi:connected:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedWifiStatusInfoCompanion *companion __attribute__((swift_name("companion")));
- (SharedWifiStatusInfo *)doCopySsid:(NSString *)ssid ip:(NSString *)ip rssi:(int32_t)rssi connected:(BOOL)connected __attribute__((swift_name("doCopy(ssid:ip:rssi:connected:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) BOOL connected __attribute__((swift_name("connected")));
@property (readonly) NSString *ip __attribute__((swift_name("ip")));
@property (readonly) int32_t rssi __attribute__((swift_name("rssi")));
@property (readonly) NSString *ssid __attribute__((swift_name("ssid")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("WifiStatusInfo.Companion")))
@interface SharedWifiStatusInfoCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedWifiStatusInfoCompanion *shared __attribute__((swift_name("shared")));
- (id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("serializer()")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("AuditUseCase")))
@interface SharedAuditUseCase : SharedBase
- (instancetype)initWithDb:(SharedDatabaseHelper *)db __attribute__((swift_name("init(db:)"))) __attribute__((objc_designated_initializer));
- (NSArray<SharedAuditEvent *> *)getEventsAction:(NSString * _Nullable)action batteryIndex:(SharedInt * _Nullable)batteryIndex __attribute__((swift_name("getEvents(action:batteryIndex:)")));
- (int64_t)getPendingSyncCount __attribute__((swift_name("getPendingSyncCount()")));
- (void)recordUserId:(NSString *)userId action:(NSString *)action target:(SharedInt * _Nullable)target detail:(NSString * _Nullable)detail __attribute__((swift_name("record(userId:action:target:detail:)")));
@end

__attribute__((swift_name("SyncCloud")))
@protocol SharedSyncCloud
@required

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)syncBatchPayload:(NSString *)payload completionHandler:(void (^)(SharedBoolean * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("syncBatch(payload:completionHandler:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SyncManager")))
@interface SharedSyncManager : SharedBase
- (instancetype)initWithDb:(SharedDatabaseHelper *)db restClient:(SharedCloudRestClient * _Nullable)restClient __attribute__((swift_name("init(db:restClient:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithStore:(id<SharedSyncStore>)store cloud:(id<SharedSyncCloud> _Nullable)cloud __attribute__((swift_name("init(store:cloud:)"))) __attribute__((objc_designated_initializer));
- (void)close __attribute__((swift_name("close()")));
- (int64_t)currentRetryDelayMs __attribute__((swift_name("currentRetryDelayMs()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)runSyncCycleWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("runSyncCycle(completionHandler:)")));
- (void)start __attribute__((swift_name("start()")));
@end

__attribute__((swift_name("SyncStore")))
@protocol SharedSyncStore
@required
- (int64_t)countUnsyncedAudit __attribute__((swift_name("countUnsyncedAudit()")));
- (NSArray<SharedPendingAuditSyncItem *> *)getUnsyncedAuditLimit:(int32_t)limit __attribute__((swift_name("getUnsyncedAudit(limit:)")));
- (void)markAuditSyncedIds:(NSArray<SharedLong *> *)ids __attribute__((swift_name("markAuditSynced(ids:)")));
@end

__attribute__((swift_name("Transport")))
@protocol SharedTransport
@required
- (void)close __attribute__((swift_name("close()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)connectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("connect(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)disconnectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("disconnect(completionHandler:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeHealth __attribute__((swift_name("observeHealth()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));
- (BOOL)supportsCapability:(SharedTransportCapability *)capability __attribute__((swift_name("supports(capability:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)triggerRintMeasurementBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("triggerRintMeasurement(batteryIndex:completionHandler:)")));
@property (readonly) NSSet<SharedTransportCapability *> *capabilities __attribute__((swift_name("capabilities")));
@property (readonly) SharedTransportChannel *channel __attribute__((swift_name("channel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isConnected __attribute__((swift_name("isConnected")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BleTransport")))
@interface SharedBleTransport : SharedBase <SharedTransport>
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
@property (class, readonly, getter=companion) SharedBleTransportCompanion *companion __attribute__((swift_name("companion")));
- (void)close __attribute__((swift_name("close()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)connectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("connect(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)disconnectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("disconnect(completionHandler:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeHealth __attribute__((swift_name("observeHealth()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)triggerRintMeasurementBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("triggerRintMeasurement(batteryIndex:completionHandler:)")));
@property (readonly) NSSet<SharedTransportCapability *> *capabilities __attribute__((swift_name("capabilities")));
@property (readonly) SharedTransportChannel *channel __attribute__((swift_name("channel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isConnected __attribute__((swift_name("isConnected")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("BleTransport.Companion")))
@interface SharedBleTransportCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedBleTransportCompanion *shared __attribute__((swift_name("shared")));
@property (readonly) NSString *BATTERY_SVC __attribute__((swift_name("BATTERY_SVC")));
@property (readonly) NSString *CONTROL_SVC __attribute__((swift_name("CONTROL_SVC")));
@property (readonly) NSString *SYSTEM_SVC __attribute__((swift_name("SYSTEM_SVC")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("CloudRestClient")))
@interface SharedCloudRestClient : SharedBase
- (instancetype)initWithBaseUrl:(NSString *)baseUrl apiKey:(NSString *)apiKey __attribute__((swift_name("init(baseUrl:apiKey:)"))) __attribute__((objc_designated_initializer));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getAuditEventsFromMs:(SharedLong * _Nullable)fromMs toMs:(SharedLong * _Nullable)toMs user:(NSString * _Nullable)user action:(NSString * _Nullable)action completionHandler:(void (^)(NSArray<SharedAuditEvent *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getAuditEvents(fromMs:toMs:user:action:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getBatteriesWithCompletionHandler:(void (^)(NSArray<SharedBatteryState *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getBatteries(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getHistoryBatteryIndex:(int32_t)batteryIndex fromMs:(int64_t)fromMs toMs:(int64_t)toMs completionHandler:(void (^)(NSArray<SharedBatteryHistoryPoint *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getHistory(batteryIndex:fromMs:toMs:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)syncBatchPayload:(NSString *)payload completionHandler:(void (^)(SharedBoolean * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("syncBatch(payload:completionHandler:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("GattParser")))
@interface SharedGattParser : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)gattParser __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedGattParser *shared __attribute__((swift_name("shared")));
- (SharedKotlinByteArray *)encodeConfigConfig:(SharedProtectionConfig *)config __attribute__((swift_name("encodeConfig(config:)")));
- (SharedKotlinByteArray *)encodeResetBatteryIndex:(int32_t)batteryIndex __attribute__((swift_name("encodeReset(batteryIndex:)")));
- (SharedKotlinByteArray *)encodeRintTriggerBatteryIndex:(int32_t)batteryIndex __attribute__((swift_name("encodeRintTrigger(batteryIndex:)")));
- (SharedKotlinByteArray *)encodeSwitchBatteryIndex:(int32_t)batteryIndex on:(BOOL)on __attribute__((swift_name("encodeSwitch(batteryIndex:on:)")));
- (SharedKotlinByteArray *)encodeWifiConfigSsid:(NSString *)ssid password:(NSString *)password __attribute__((swift_name("encodeWifiConfig(ssid:password:)")));
- (SharedBatteryState *)parseBatteryIndex:(int32_t)index bytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseBattery(index:bytes:)")));
- (SharedCommandResult *)parseCommandStatusBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseCommandStatus(bytes:)")));
- (NSArray<SharedGattParserRintResult *> *)parseRintAllBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseRintAll(bytes:)")));
- (SharedGattParserRintResult *)parseRintSingleIndex:(int32_t)index bytes:(SharedKotlinByteArray *)bytes offset:(int32_t)offset __attribute__((swift_name("parseRintSingle(index:bytes:offset:)")));
- (NSArray<SharedBatteryHealth *> *)parseSohAllBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseSohAll(bytes:)")));
- (SharedBatteryHealth *)parseSohSingleIndex:(int32_t)index bytes:(SharedKotlinByteArray *)bytes offset:(int32_t)offset __attribute__((swift_name("parseSohSingle(index:bytes:offset:)")));
- (SharedSolarData *)parseSolarBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseSolar(bytes:)")));
- (SharedKotlinTriple<SharedInt *, SharedInt *, SharedBoolean *> *)parseTopologyBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseTopology(bytes:)")));
- (int64_t)parseUint32Bytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseUint32(bytes:)")));
- (SharedWifiStatusInfo *)parseWifiStatusBytes:(SharedKotlinByteArray *)bytes __attribute__((swift_name("parseWifiStatus(bytes:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("GattParser.RintResult")))
@interface SharedGattParserRintResult : SharedBase
- (instancetype)initWithIndex:(int32_t)index rOhmicMohm:(float)rOhmicMohm rTotalMohm:(float)rTotalMohm vLoadMv:(int32_t)vLoadMv vOcvMv:(int32_t)vOcvMv iLoadMa:(int32_t)iLoadMa rintValid:(BOOL)rintValid __attribute__((swift_name("init(index:rOhmicMohm:rTotalMohm:vLoadMv:vOcvMv:iLoadMa:rintValid:)"))) __attribute__((objc_designated_initializer));
- (SharedGattParserRintResult *)doCopyIndex:(int32_t)index rOhmicMohm:(float)rOhmicMohm rTotalMohm:(float)rTotalMohm vLoadMv:(int32_t)vLoadMv vOcvMv:(int32_t)vOcvMv iLoadMa:(int32_t)iLoadMa rintValid:(BOOL)rintValid __attribute__((swift_name("doCopy(index:rOhmicMohm:rTotalMohm:vLoadMv:vOcvMv:iLoadMa:rintValid:)")));
- (BOOL)isEqual:(id _Nullable)other __attribute__((swift_name("isEqual(_:)")));
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) int32_t iLoadMa __attribute__((swift_name("iLoadMa")));
@property (readonly) int32_t index __attribute__((swift_name("index")));
@property (readonly) float rOhmicMohm __attribute__((swift_name("rOhmicMohm")));
@property (readonly) float rTotalMohm __attribute__((swift_name("rTotalMohm")));
@property (readonly) BOOL rintValid __attribute__((swift_name("rintValid")));
@property (readonly) int32_t vLoadMv __attribute__((swift_name("vLoadMv")));
@property (readonly) int32_t vOcvMv __attribute__((swift_name("vOcvMv")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("MqttTransport")))
@interface SharedMqttTransport : SharedBase <SharedTransport>
- (instancetype)initWithBrokerUrl:(NSString *)brokerUrl username:(NSString *)username password:(NSString *)password __attribute__((swift_name("init(brokerUrl:username:password:)"))) __attribute__((objc_designated_initializer));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)connectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("connect(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)disconnectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("disconnect(completionHandler:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));
- (void)onMqttMessageTopic:(NSString *)topic payload:(NSString *)payload __attribute__((swift_name("onMqttMessage(topic:payload:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));
- (void)setConnectedConnected:(BOOL)connected __attribute__((swift_name("setConnected(connected:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));
@property (readonly) NSSet<SharedTransportCapability *> *capabilities __attribute__((swift_name("capabilities")));
@property (readonly) SharedTransportChannel *channel __attribute__((swift_name("channel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isConnected __attribute__((swift_name("isConnected")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("OfflineTransport")))
@interface SharedOfflineTransport : SharedBase <SharedTransport>
- (instancetype)initWithDb:(SharedDatabaseHelper *)db __attribute__((swift_name("init(db:)"))) __attribute__((objc_designated_initializer));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)connectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("connect(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)disconnectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("disconnect(completionHandler:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));
@property (readonly) NSSet<SharedTransportCapability *> *capabilities __attribute__((swift_name("capabilities")));
@property (readonly) SharedTransportChannel *channel __attribute__((swift_name("channel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isConnected __attribute__((swift_name("isConnected")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("SohRestClient")))
@interface SharedSohRestClient : SharedBase
- (instancetype)initWithBaseUrl:(NSString *)baseUrl apiKey:(NSString *)apiKey __attribute__((swift_name("init(baseUrl:apiKey:)"))) __attribute__((objc_designated_initializer));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)forceRefreshWithCompletionHandler:(void (^)(SharedBoolean * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("forceRefresh(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getAllScoresWithCompletionHandler:(void (^)(NSArray<SharedMlScore *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getAllScores(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getBatteryScoreBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedMlScore * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getBatteryScore(batteryIndex:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getDiagnosticBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedDiagnostic * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getDiagnostic(batteryIndex:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getFleetDiagnosticWithCompletionHandler:(void (^)(SharedFleetDiagnostic * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getFleetDiagnostic(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getFleetHealthWithCompletionHandler:(void (^)(SharedFleetHealth * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getFleetHealth(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)getSohHistoryBatteryIndex:(int32_t)batteryIndex days:(int32_t)days completionHandler:(void (^)(NSArray<SharedMlScore *> * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("getSohHistory(batteryIndex:days:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)refreshDiagnosticBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedDiagnostic * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("refreshDiagnostic(batteryIndex:completionHandler:)")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("TransportCapability")))
@interface SharedTransportCapability : SharedKotlinEnum<SharedTransportCapability *>
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (instancetype)initWithName:(NSString *)name ordinal:(int32_t)ordinal __attribute__((swift_name("init(name:ordinal:)"))) __attribute__((objc_designated_initializer)) __attribute__((unavailable));
@property (class, readonly) SharedTransportCapability *observe __attribute__((swift_name("observe")));
@property (class, readonly) SharedTransportCapability *switchBattery __attribute__((swift_name("switchBattery")));
@property (class, readonly) SharedTransportCapability *resetSwitch __attribute__((swift_name("resetSwitch")));
@property (class, readonly) SharedTransportCapability *setConfig __attribute__((swift_name("setConfig")));
@property (class, readonly) SharedTransportCapability *setWifi __attribute__((swift_name("setWifi")));
+ (SharedKotlinArray<SharedTransportCapability *> *)values __attribute__((swift_name("values()")));
@property (class, readonly) NSArray<SharedTransportCapability *> *entries __attribute__((swift_name("entries")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("TransportManager")))
@interface SharedTransportManager : SharedBase
- (instancetype)initWithBle:(id<SharedTransport>)ble wifi:(id<SharedTransport> _Nullable)wifi mqtt:(id<SharedTransport> _Nullable)mqtt offline:(id<SharedTransport>)offline __attribute__((swift_name("init(ble:wifi:mqtt:offline:)"))) __attribute__((objc_designated_initializer));
@property (class, readonly, getter=companion) SharedTransportManagerCompanion *companion __attribute__((swift_name("companion")));
- (void)close __attribute__((swift_name("close()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeHealth __attribute__((swift_name("observeHealth()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));
- (void)setMqttTransport:(SharedMqttTransport * _Nullable)transport __attribute__((swift_name("setMqtt(transport:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));
- (void)setWifiTransport:(SharedWifiTransport * _Nullable)transport __attribute__((swift_name("setWifi(transport:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));
- (void)start __attribute__((swift_name("start()")));
- (BOOL)supportsCapability:(SharedTransportCapability *)capability __attribute__((swift_name("supports(capability:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)triggerRintMeasurementBatteryIndex:(int32_t)batteryIndex completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("triggerRintMeasurement(batteryIndex:completionHandler:)")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> activeChannel __attribute__((swift_name("activeChannel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> activeTransport __attribute__((swift_name("activeTransport")));
@property SharedTransportChannel * _Nullable forcedChannel __attribute__((swift_name("forcedChannel")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("TransportManager.Companion")))
@interface SharedTransportManagerCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedTransportManagerCompanion *shared __attribute__((swift_name("shared")));
@property (readonly) NSArray<SharedTransportChannel *> *PRIORITY_ORDER __attribute__((swift_name("PRIORITY_ORDER")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("WifiTransport")))
@interface SharedWifiTransport : SharedBase <SharedTransport>
- (instancetype)initWithBaseUrl:(NSString *)baseUrl token:(NSString *)token __attribute__((swift_name("init(baseUrl:token:)"))) __attribute__((objc_designated_initializer));
- (void)close __attribute__((swift_name("close()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)connectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("connect(completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)disconnectWithCompletionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("disconnect(completionHandler:)")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeBatteries __attribute__((swift_name("observeBatteries()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSolar __attribute__((swift_name("observeSolar()")));
- (id<SharedKotlinx_coroutines_coreFlow>)observeSystem __attribute__((swift_name("observeSystem()")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)resetSwitchCountIndex:(int32_t)index completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("resetSwitchCount(index:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setProtectionConfigConfig:(SharedProtectionConfig *)config completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setProtectionConfig(config:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)setWifiConfigSsid:(NSString *)ssid password:(NSString *)password completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("setWifiConfig(ssid:password:completionHandler:)")));

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)switchBatteryIndex:(int32_t)index on:(BOOL)on completionHandler:(void (^)(SharedCommandResult * _Nullable, NSError * _Nullable))completionHandler __attribute__((swift_name("switchBattery(index:on:completionHandler:)")));
@property (readonly) NSSet<SharedTransportCapability *> *capabilities __attribute__((swift_name("capabilities")));
@property (readonly) SharedTransportChannel *channel __attribute__((swift_name("channel")));
@property (readonly) id<SharedKotlinx_coroutines_coreStateFlow> isConnected __attribute__((swift_name("isConnected")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("CurrentTimeKt")))
@interface SharedCurrentTimeKt : SharedBase
+ (int64_t)currentTimeMillis __attribute__((swift_name("currentTimeMillis()")));
@end

__attribute__((swift_name("RuntimeTransactionCallbacks")))
@protocol SharedRuntimeTransactionCallbacks
@required
- (void)afterCommitFunction:(void (^)(void))function __attribute__((swift_name("afterCommit(function:)")));
- (void)afterRollbackFunction:(void (^)(void))function __attribute__((swift_name("afterRollback(function:)")));
@end

__attribute__((swift_name("RuntimeTransactionWithoutReturn")))
@protocol SharedRuntimeTransactionWithoutReturn <SharedRuntimeTransactionCallbacks>
@required
- (void)rollback __attribute__((swift_name("rollback()")));
- (void)transactionBody:(void (^)(id<SharedRuntimeTransactionWithoutReturn>))body __attribute__((swift_name("transaction(body:)")));
@end

__attribute__((swift_name("RuntimeTransactionWithReturn")))
@protocol SharedRuntimeTransactionWithReturn <SharedRuntimeTransactionCallbacks>
@required
- (void)rollbackReturnValue:(id _Nullable)returnValue __attribute__((swift_name("rollback(returnValue:)")));
- (id _Nullable)transactionBody_:(id _Nullable (^)(id<SharedRuntimeTransactionWithReturn>))body __attribute__((swift_name("transaction(body_:)")));
@end

__attribute__((swift_name("RuntimeCloseable")))
@protocol SharedRuntimeCloseable
@required
- (void)close __attribute__((swift_name("close()")));
@end

__attribute__((swift_name("RuntimeSqlDriver")))
@protocol SharedRuntimeSqlDriver <SharedRuntimeCloseable>
@required
- (void)addListenerQueryKeys:(SharedKotlinArray<NSString *> *)queryKeys listener:(id<SharedRuntimeQueryListener>)listener __attribute__((swift_name("addListener(queryKeys:listener:)")));
- (SharedRuntimeTransacterTransaction * _Nullable)currentTransaction __attribute__((swift_name("currentTransaction()")));
- (id<SharedRuntimeQueryResult>)executeIdentifier:(SharedInt * _Nullable)identifier sql:(NSString *)sql parameters:(int32_t)parameters binders:(void (^ _Nullable)(id<SharedRuntimeSqlPreparedStatement>))binders __attribute__((swift_name("execute(identifier:sql:parameters:binders:)")));
- (id<SharedRuntimeQueryResult>)executeQueryIdentifier:(SharedInt * _Nullable)identifier sql:(NSString *)sql mapper:(id<SharedRuntimeQueryResult> (^)(id<SharedRuntimeSqlCursor>))mapper parameters:(int32_t)parameters binders:(void (^ _Nullable)(id<SharedRuntimeSqlPreparedStatement>))binders __attribute__((swift_name("executeQuery(identifier:sql:mapper:parameters:binders:)")));
- (id<SharedRuntimeQueryResult>)doNewTransaction __attribute__((swift_name("doNewTransaction()")));
- (void)notifyListenersQueryKeys:(SharedKotlinArray<NSString *> *)queryKeys __attribute__((swift_name("notifyListeners(queryKeys:)")));
- (void)removeListenerQueryKeys:(SharedKotlinArray<NSString *> *)queryKeys listener:(id<SharedRuntimeQueryListener>)listener __attribute__((swift_name("removeListener(queryKeys:listener:)")));
@end

__attribute__((swift_name("RuntimeSqlSchema")))
@protocol SharedRuntimeSqlSchema
@required
- (id<SharedRuntimeQueryResult>)createDriver:(id<SharedRuntimeSqlDriver>)driver __attribute__((swift_name("create(driver:)")));
- (id<SharedRuntimeQueryResult>)migrateDriver:(id<SharedRuntimeSqlDriver>)driver oldVersion:(int64_t)oldVersion newVersion:(int64_t)newVersion callbacks:(SharedKotlinArray<SharedRuntimeAfterVersion *> *)callbacks __attribute__((swift_name("migrate(driver:oldVersion:newVersion:callbacks:)")));
@property (readonly) int64_t version __attribute__((swift_name("version")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinUnit")))
@interface SharedKotlinUnit : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)unit __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedKotlinUnit *shared __attribute__((swift_name("shared")));
- (NSString *)description __attribute__((swift_name("description()")));
@end

__attribute__((swift_name("RuntimeTransacterTransaction")))
@interface SharedRuntimeTransacterTransaction : SharedBase <SharedRuntimeTransactionCallbacks>
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (void)afterCommitFunction:(void (^)(void))function __attribute__((swift_name("afterCommit(function:)")));
- (void)afterRollbackFunction:(void (^)(void))function __attribute__((swift_name("afterRollback(function:)")));

/**
 * @note This method has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
- (id<SharedRuntimeQueryResult>)endTransactionSuccessful:(BOOL)successful __attribute__((swift_name("endTransaction(successful:)")));

/**
 * @note This property has protected visibility in Kotlin source and is intended only for use by subclasses.
*/
@property (readonly) SharedRuntimeTransacterTransaction * _Nullable enclosingTransaction __attribute__((swift_name("enclosingTransaction")));
@end

__attribute__((swift_name("KotlinThrowable")))
@interface SharedKotlinThrowable : SharedBase
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (instancetype)initWithMessage:(NSString * _Nullable)message __attribute__((swift_name("init(message:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithCause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(cause:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithMessage:(NSString * _Nullable)message cause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(message:cause:)"))) __attribute__((objc_designated_initializer));

/**
 * @note annotations
 *   kotlin.experimental.ExperimentalNativeApi
*/
- (SharedKotlinArray<NSString *> *)getStackTrace __attribute__((swift_name("getStackTrace()")));
- (void)printStackTrace __attribute__((swift_name("printStackTrace()")));
- (NSString *)description __attribute__((swift_name("description()")));
@property (readonly) SharedKotlinThrowable * _Nullable cause __attribute__((swift_name("cause")));
@property (readonly) NSString * _Nullable message __attribute__((swift_name("message")));
- (NSError *)asError __attribute__((swift_name("asError()")));
@end

__attribute__((swift_name("RuntimeExecutableQuery")))
@interface SharedRuntimeExecutableQuery<__covariant RowType> : SharedBase
- (instancetype)initWithMapper:(RowType (^)(id<SharedRuntimeSqlCursor>))mapper __attribute__((swift_name("init(mapper:)"))) __attribute__((objc_designated_initializer));
- (id<SharedRuntimeQueryResult>)executeMapper:(id<SharedRuntimeQueryResult> (^)(id<SharedRuntimeSqlCursor>))mapper __attribute__((swift_name("execute(mapper:)")));
- (NSArray<RowType> *)executeAsList __attribute__((swift_name("executeAsList()")));
- (RowType)executeAsOne __attribute__((swift_name("executeAsOne()")));
- (RowType _Nullable)executeAsOneOrNull __attribute__((swift_name("executeAsOneOrNull()")));
@property (readonly) RowType (^mapper)(id<SharedRuntimeSqlCursor>) __attribute__((swift_name("mapper")));
@end

__attribute__((swift_name("RuntimeQuery")))
@interface SharedRuntimeQuery<__covariant RowType> : SharedRuntimeExecutableQuery<RowType>
- (instancetype)initWithMapper:(RowType (^)(id<SharedRuntimeSqlCursor>))mapper __attribute__((swift_name("init(mapper:)"))) __attribute__((objc_designated_initializer));
- (void)addListenerListener:(id<SharedRuntimeQueryListener>)listener __attribute__((swift_name("addListener(listener:)")));
- (void)removeListenerListener:(id<SharedRuntimeQueryListener>)listener __attribute__((swift_name("removeListener(listener:)")));
@end

__attribute__((swift_name("KotlinException")))
@interface SharedKotlinException : SharedKotlinThrowable
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (instancetype)initWithMessage:(NSString * _Nullable)message __attribute__((swift_name("init(message:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithCause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(cause:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithMessage:(NSString * _Nullable)message cause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(message:cause:)"))) __attribute__((objc_designated_initializer));
@end

__attribute__((swift_name("KotlinRuntimeException")))
@interface SharedKotlinRuntimeException : SharedKotlinException
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (instancetype)initWithMessage:(NSString * _Nullable)message __attribute__((swift_name("init(message:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithCause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(cause:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithMessage:(NSString * _Nullable)message cause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(message:cause:)"))) __attribute__((objc_designated_initializer));
@end

__attribute__((swift_name("KotlinIllegalStateException")))
@interface SharedKotlinIllegalStateException : SharedKotlinRuntimeException
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (instancetype)initWithMessage:(NSString * _Nullable)message __attribute__((swift_name("init(message:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithCause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(cause:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithMessage:(NSString * _Nullable)message cause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(message:cause:)"))) __attribute__((objc_designated_initializer));
@end


/**
 * @note annotations
 *   kotlin.SinceKotlin(version="1.4")
*/
__attribute__((swift_name("KotlinCancellationException")))
@interface SharedKotlinCancellationException : SharedKotlinIllegalStateException
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (instancetype)initWithMessage:(NSString * _Nullable)message __attribute__((swift_name("init(message:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithCause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(cause:)"))) __attribute__((objc_designated_initializer));
- (instancetype)initWithMessage:(NSString * _Nullable)message cause:(SharedKotlinThrowable * _Nullable)cause __attribute__((swift_name("init(message:cause:)"))) __attribute__((objc_designated_initializer));
@end

__attribute__((swift_name("Kotlinx_coroutines_coreFlow")))
@protocol SharedKotlinx_coroutines_coreFlow
@required

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)collectCollector:(id<SharedKotlinx_coroutines_coreFlowCollector>)collector completionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("collect(collector:completionHandler:)")));
@end

__attribute__((swift_name("Kotlinx_coroutines_coreSharedFlow")))
@protocol SharedKotlinx_coroutines_coreSharedFlow <SharedKotlinx_coroutines_coreFlow>
@required
@property (readonly) NSArray<id> *replayCache __attribute__((swift_name("replayCache")));
@end

__attribute__((swift_name("Kotlinx_coroutines_coreStateFlow")))
@protocol SharedKotlinx_coroutines_coreStateFlow <SharedKotlinx_coroutines_coreSharedFlow>
@required
@property (readonly) id _Nullable value __attribute__((swift_name("value")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreSerializationStrategy")))
@protocol SharedKotlinx_serialization_coreSerializationStrategy
@required
- (void)serializeEncoder:(id<SharedKotlinx_serialization_coreEncoder>)encoder value:(id _Nullable)value __attribute__((swift_name("serialize(encoder:value:)")));
@property (readonly) id<SharedKotlinx_serialization_coreSerialDescriptor> descriptor __attribute__((swift_name("descriptor")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreDeserializationStrategy")))
@protocol SharedKotlinx_serialization_coreDeserializationStrategy
@required
- (id _Nullable)deserializeDecoder:(id<SharedKotlinx_serialization_coreDecoder>)decoder __attribute__((swift_name("deserialize(decoder:)")));
@property (readonly) id<SharedKotlinx_serialization_coreSerialDescriptor> descriptor __attribute__((swift_name("descriptor")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreKSerializer")))
@protocol SharedKotlinx_serialization_coreKSerializer <SharedKotlinx_serialization_coreSerializationStrategy, SharedKotlinx_serialization_coreDeserializationStrategy>
@required
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinEnumCompanion")))
@interface SharedKotlinEnumCompanion : SharedBase
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
+ (instancetype)companion __attribute__((swift_name("init()")));
@property (class, readonly, getter=shared) SharedKotlinEnumCompanion *shared __attribute__((swift_name("shared")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinArray")))
@interface SharedKotlinArray<T> : SharedBase
+ (instancetype)arrayWithSize:(int32_t)size init:(T _Nullable (^)(SharedInt *))init __attribute__((swift_name("init(size:init:)")));
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (T _Nullable)getIndex:(int32_t)index __attribute__((swift_name("get(index:)")));
- (id<SharedKotlinIterator>)iterator __attribute__((swift_name("iterator()")));
- (void)setIndex:(int32_t)index value:(T _Nullable)value __attribute__((swift_name("set(index:value:)")));
@property (readonly) int32_t size __attribute__((swift_name("size")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinByteArray")))
@interface SharedKotlinByteArray : SharedBase
+ (instancetype)arrayWithSize:(int32_t)size __attribute__((swift_name("init(size:)")));
+ (instancetype)arrayWithSize:(int32_t)size init:(SharedByte *(^)(SharedInt *))init __attribute__((swift_name("init(size:init:)")));
+ (instancetype)alloc __attribute__((unavailable));
+ (instancetype)allocWithZone:(struct _NSZone *)zone __attribute__((unavailable));
- (int8_t)getIndex:(int32_t)index __attribute__((swift_name("get(index:)")));
- (SharedKotlinByteIterator *)iterator __attribute__((swift_name("iterator()")));
- (void)setIndex:(int32_t)index value:(int8_t)value __attribute__((swift_name("set(index:value:)")));
@property (readonly) int32_t size __attribute__((swift_name("size")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinTriple")))
@interface SharedKotlinTriple<__covariant A, __covariant B, __covariant C> : SharedBase
- (instancetype)initWithFirst:(A _Nullable)first second:(B _Nullable)second third:(C _Nullable)third __attribute__((swift_name("init(first:second:third:)"))) __attribute__((objc_designated_initializer));
- (SharedKotlinTriple<A, B, C> *)doCopyFirst:(A _Nullable)first second:(B _Nullable)second third:(C _Nullable)third __attribute__((swift_name("doCopy(first:second:third:)")));
- (BOOL)equalsOther:(id _Nullable)other __attribute__((swift_name("equals(other:)")));
- (int32_t)hashCode __attribute__((swift_name("hashCode()")));
- (NSString *)toString __attribute__((swift_name("toString()")));
@property (readonly) A _Nullable first __attribute__((swift_name("first")));
@property (readonly) B _Nullable second __attribute__((swift_name("second")));
@property (readonly) C _Nullable third __attribute__((swift_name("third")));
@end

__attribute__((swift_name("RuntimeQueryListener")))
@protocol SharedRuntimeQueryListener
@required
- (void)queryResultsChanged __attribute__((swift_name("queryResultsChanged()")));
@end

__attribute__((swift_name("RuntimeQueryResult")))
@protocol SharedRuntimeQueryResult
@required

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)awaitWithCompletionHandler:(void (^)(id _Nullable_result, NSError * _Nullable))completionHandler __attribute__((swift_name("await(completionHandler:)")));
@property (readonly) id _Nullable value __attribute__((swift_name("value")));
@end

__attribute__((swift_name("RuntimeSqlPreparedStatement")))
@protocol SharedRuntimeSqlPreparedStatement
@required
- (void)bindBooleanIndex:(int32_t)index boolean:(SharedBoolean * _Nullable)boolean __attribute__((swift_name("bindBoolean(index:boolean:)")));
- (void)bindBytesIndex:(int32_t)index bytes:(SharedKotlinByteArray * _Nullable)bytes __attribute__((swift_name("bindBytes(index:bytes:)")));
- (void)bindDoubleIndex:(int32_t)index double:(SharedDouble * _Nullable)double_ __attribute__((swift_name("bindDouble(index:double:)")));
- (void)bindLongIndex:(int32_t)index long:(SharedLong * _Nullable)long_ __attribute__((swift_name("bindLong(index:long:)")));
- (void)bindStringIndex:(int32_t)index string:(NSString * _Nullable)string __attribute__((swift_name("bindString(index:string:)")));
@end

__attribute__((swift_name("RuntimeSqlCursor")))
@protocol SharedRuntimeSqlCursor
@required
- (SharedBoolean * _Nullable)getBooleanIndex:(int32_t)index __attribute__((swift_name("getBoolean(index:)")));
- (SharedKotlinByteArray * _Nullable)getBytesIndex:(int32_t)index __attribute__((swift_name("getBytes(index:)")));
- (SharedDouble * _Nullable)getDoubleIndex:(int32_t)index __attribute__((swift_name("getDouble(index:)")));
- (SharedLong * _Nullable)getLongIndex:(int32_t)index __attribute__((swift_name("getLong(index:)")));
- (NSString * _Nullable)getStringIndex:(int32_t)index __attribute__((swift_name("getString(index:)")));
- (id<SharedRuntimeQueryResult>)next __attribute__((swift_name("next()")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("RuntimeAfterVersion")))
@interface SharedRuntimeAfterVersion : SharedBase
- (instancetype)initWithAfterVersion:(int64_t)afterVersion block:(void (^)(id<SharedRuntimeSqlDriver>))block __attribute__((swift_name("init(afterVersion:block:)"))) __attribute__((objc_designated_initializer));
@property (readonly) int64_t afterVersion __attribute__((swift_name("afterVersion")));
@property (readonly) void (^block)(id<SharedRuntimeSqlDriver>) __attribute__((swift_name("block")));
@end

__attribute__((swift_name("Kotlinx_coroutines_coreFlowCollector")))
@protocol SharedKotlinx_coroutines_coreFlowCollector
@required

/**
 * @note This method converts instances of CancellationException to errors.
 * Other uncaught Kotlin exceptions are fatal.
*/
- (void)emitValue:(id _Nullable)value completionHandler:(void (^)(NSError * _Nullable))completionHandler __attribute__((swift_name("emit(value:completionHandler:)")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreEncoder")))
@protocol SharedKotlinx_serialization_coreEncoder
@required
- (id<SharedKotlinx_serialization_coreCompositeEncoder>)beginCollectionDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor collectionSize:(int32_t)collectionSize __attribute__((swift_name("beginCollection(descriptor:collectionSize:)")));
- (id<SharedKotlinx_serialization_coreCompositeEncoder>)beginStructureDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("beginStructure(descriptor:)")));
- (void)encodeBooleanValue:(BOOL)value __attribute__((swift_name("encodeBoolean(value:)")));
- (void)encodeByteValue:(int8_t)value __attribute__((swift_name("encodeByte(value:)")));
- (void)encodeCharValue:(unichar)value __attribute__((swift_name("encodeChar(value:)")));
- (void)encodeDoubleValue:(double)value __attribute__((swift_name("encodeDouble(value:)")));
- (void)encodeEnumEnumDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)enumDescriptor index:(int32_t)index __attribute__((swift_name("encodeEnum(enumDescriptor:index:)")));
- (void)encodeFloatValue:(float)value __attribute__((swift_name("encodeFloat(value:)")));
- (id<SharedKotlinx_serialization_coreEncoder>)encodeInlineDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("encodeInline(descriptor:)")));
- (void)encodeIntValue:(int32_t)value __attribute__((swift_name("encodeInt(value:)")));
- (void)encodeLongValue:(int64_t)value __attribute__((swift_name("encodeLong(value:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (void)encodeNotNullMark __attribute__((swift_name("encodeNotNullMark()")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (void)encodeNull __attribute__((swift_name("encodeNull()")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (void)encodeNullableSerializableValueSerializer:(id<SharedKotlinx_serialization_coreSerializationStrategy>)serializer value:(id _Nullable)value __attribute__((swift_name("encodeNullableSerializableValue(serializer:value:)")));
- (void)encodeSerializableValueSerializer:(id<SharedKotlinx_serialization_coreSerializationStrategy>)serializer value:(id _Nullable)value __attribute__((swift_name("encodeSerializableValue(serializer:value:)")));
- (void)encodeShortValue:(int16_t)value __attribute__((swift_name("encodeShort(value:)")));
- (void)encodeStringValue:(NSString *)value __attribute__((swift_name("encodeString(value:)")));
@property (readonly) SharedKotlinx_serialization_coreSerializersModule *serializersModule __attribute__((swift_name("serializersModule")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreSerialDescriptor")))
@protocol SharedKotlinx_serialization_coreSerialDescriptor
@required

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (NSArray<id<SharedKotlinAnnotation>> *)getElementAnnotationsIndex:(int32_t)index __attribute__((swift_name("getElementAnnotations(index:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id<SharedKotlinx_serialization_coreSerialDescriptor>)getElementDescriptorIndex:(int32_t)index __attribute__((swift_name("getElementDescriptor(index:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (int32_t)getElementIndexName:(NSString *)name __attribute__((swift_name("getElementIndex(name:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (NSString *)getElementNameIndex:(int32_t)index __attribute__((swift_name("getElementName(index:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (BOOL)isElementOptionalIndex:(int32_t)index __attribute__((swift_name("isElementOptional(index:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
@property (readonly) NSArray<id<SharedKotlinAnnotation>> *annotations __attribute__((swift_name("annotations")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
@property (readonly) int32_t elementsCount __attribute__((swift_name("elementsCount")));
@property (readonly) BOOL isInline __attribute__((swift_name("isInline")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
@property (readonly) BOOL isNullable __attribute__((swift_name("isNullable")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
@property (readonly) SharedKotlinx_serialization_coreSerialKind *kind __attribute__((swift_name("kind")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
@property (readonly) NSString *serialName __attribute__((swift_name("serialName")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreDecoder")))
@protocol SharedKotlinx_serialization_coreDecoder
@required
- (id<SharedKotlinx_serialization_coreCompositeDecoder>)beginStructureDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("beginStructure(descriptor:)")));
- (BOOL)decodeBoolean __attribute__((swift_name("decodeBoolean()")));
- (int8_t)decodeByte __attribute__((swift_name("decodeByte()")));
- (unichar)decodeChar __attribute__((swift_name("decodeChar()")));
- (double)decodeDouble __attribute__((swift_name("decodeDouble()")));
- (int32_t)decodeEnumEnumDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)enumDescriptor __attribute__((swift_name("decodeEnum(enumDescriptor:)")));
- (float)decodeFloat __attribute__((swift_name("decodeFloat()")));
- (id<SharedKotlinx_serialization_coreDecoder>)decodeInlineDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("decodeInline(descriptor:)")));
- (int32_t)decodeInt __attribute__((swift_name("decodeInt()")));
- (int64_t)decodeLong __attribute__((swift_name("decodeLong()")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (BOOL)decodeNotNullMark __attribute__((swift_name("decodeNotNullMark()")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (SharedKotlinNothing * _Nullable)decodeNull __attribute__((swift_name("decodeNull()")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id _Nullable)decodeNullableSerializableValueDeserializer:(id<SharedKotlinx_serialization_coreDeserializationStrategy>)deserializer __attribute__((swift_name("decodeNullableSerializableValue(deserializer:)")));
- (id _Nullable)decodeSerializableValueDeserializer:(id<SharedKotlinx_serialization_coreDeserializationStrategy>)deserializer __attribute__((swift_name("decodeSerializableValue(deserializer:)")));
- (int16_t)decodeShort __attribute__((swift_name("decodeShort()")));
- (NSString *)decodeString __attribute__((swift_name("decodeString()")));
@property (readonly) SharedKotlinx_serialization_coreSerializersModule *serializersModule __attribute__((swift_name("serializersModule")));
@end

__attribute__((swift_name("KotlinIterator")))
@protocol SharedKotlinIterator
@required
- (BOOL)hasNext __attribute__((swift_name("hasNext()")));
- (id _Nullable)next __attribute__((swift_name("next()")));
@end

__attribute__((swift_name("KotlinByteIterator")))
@interface SharedKotlinByteIterator : SharedBase <SharedKotlinIterator>
- (instancetype)init __attribute__((swift_name("init()"))) __attribute__((objc_designated_initializer));
+ (instancetype)new __attribute__((availability(swift, unavailable, message="use object initializers instead")));
- (SharedByte *)next __attribute__((swift_name("next()")));
- (int8_t)nextByte __attribute__((swift_name("nextByte()")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreCompositeEncoder")))
@protocol SharedKotlinx_serialization_coreCompositeEncoder
@required
- (void)encodeBooleanElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(BOOL)value __attribute__((swift_name("encodeBooleanElement(descriptor:index:value:)")));
- (void)encodeByteElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(int8_t)value __attribute__((swift_name("encodeByteElement(descriptor:index:value:)")));
- (void)encodeCharElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(unichar)value __attribute__((swift_name("encodeCharElement(descriptor:index:value:)")));
- (void)encodeDoubleElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(double)value __attribute__((swift_name("encodeDoubleElement(descriptor:index:value:)")));
- (void)encodeFloatElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(float)value __attribute__((swift_name("encodeFloatElement(descriptor:index:value:)")));
- (id<SharedKotlinx_serialization_coreEncoder>)encodeInlineElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("encodeInlineElement(descriptor:index:)")));
- (void)encodeIntElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(int32_t)value __attribute__((swift_name("encodeIntElement(descriptor:index:value:)")));
- (void)encodeLongElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(int64_t)value __attribute__((swift_name("encodeLongElement(descriptor:index:value:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (void)encodeNullableSerializableElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index serializer:(id<SharedKotlinx_serialization_coreSerializationStrategy>)serializer value:(id _Nullable)value __attribute__((swift_name("encodeNullableSerializableElement(descriptor:index:serializer:value:)")));
- (void)encodeSerializableElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index serializer:(id<SharedKotlinx_serialization_coreSerializationStrategy>)serializer value:(id _Nullable)value __attribute__((swift_name("encodeSerializableElement(descriptor:index:serializer:value:)")));
- (void)encodeShortElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(int16_t)value __attribute__((swift_name("encodeShortElement(descriptor:index:value:)")));
- (void)encodeStringElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index value:(NSString *)value __attribute__((swift_name("encodeStringElement(descriptor:index:value:)")));
- (void)endStructureDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("endStructure(descriptor:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (BOOL)shouldEncodeElementDefaultDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("shouldEncodeElementDefault(descriptor:index:)")));
@property (readonly) SharedKotlinx_serialization_coreSerializersModule *serializersModule __attribute__((swift_name("serializersModule")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreSerializersModule")))
@interface SharedKotlinx_serialization_coreSerializersModule : SharedBase

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (void)dumpToCollector:(id<SharedKotlinx_serialization_coreSerializersModuleCollector>)collector __attribute__((swift_name("dumpTo(collector:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id<SharedKotlinx_serialization_coreKSerializer> _Nullable)getContextualKClass:(id<SharedKotlinKClass>)kClass typeArgumentsSerializers:(NSArray<id<SharedKotlinx_serialization_coreKSerializer>> *)typeArgumentsSerializers __attribute__((swift_name("getContextual(kClass:typeArgumentsSerializers:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id<SharedKotlinx_serialization_coreSerializationStrategy> _Nullable)getPolymorphicBaseClass:(id<SharedKotlinKClass>)baseClass value:(id)value __attribute__((swift_name("getPolymorphic(baseClass:value:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id<SharedKotlinx_serialization_coreDeserializationStrategy> _Nullable)getPolymorphicBaseClass:(id<SharedKotlinKClass>)baseClass serializedClassName:(NSString * _Nullable)serializedClassName __attribute__((swift_name("getPolymorphic(baseClass:serializedClassName:)")));
@end

__attribute__((swift_name("KotlinAnnotation")))
@protocol SharedKotlinAnnotation
@required
@end


/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
__attribute__((swift_name("Kotlinx_serialization_coreSerialKind")))
@interface SharedKotlinx_serialization_coreSerialKind : SharedBase
- (NSUInteger)hash __attribute__((swift_name("hash()")));
- (NSString *)description __attribute__((swift_name("description()")));
@end

__attribute__((swift_name("Kotlinx_serialization_coreCompositeDecoder")))
@protocol SharedKotlinx_serialization_coreCompositeDecoder
@required
- (BOOL)decodeBooleanElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeBooleanElement(descriptor:index:)")));
- (int8_t)decodeByteElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeByteElement(descriptor:index:)")));
- (unichar)decodeCharElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeCharElement(descriptor:index:)")));
- (int32_t)decodeCollectionSizeDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("decodeCollectionSize(descriptor:)")));
- (double)decodeDoubleElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeDoubleElement(descriptor:index:)")));
- (int32_t)decodeElementIndexDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("decodeElementIndex(descriptor:)")));
- (float)decodeFloatElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeFloatElement(descriptor:index:)")));
- (id<SharedKotlinx_serialization_coreDecoder>)decodeInlineElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeInlineElement(descriptor:index:)")));
- (int32_t)decodeIntElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeIntElement(descriptor:index:)")));
- (int64_t)decodeLongElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeLongElement(descriptor:index:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (id _Nullable)decodeNullableSerializableElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index deserializer:(id<SharedKotlinx_serialization_coreDeserializationStrategy>)deserializer previousValue:(id _Nullable)previousValue __attribute__((swift_name("decodeNullableSerializableElement(descriptor:index:deserializer:previousValue:)")));

/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
- (BOOL)decodeSequentially __attribute__((swift_name("decodeSequentially()")));
- (id _Nullable)decodeSerializableElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index deserializer:(id<SharedKotlinx_serialization_coreDeserializationStrategy>)deserializer previousValue:(id _Nullable)previousValue __attribute__((swift_name("decodeSerializableElement(descriptor:index:deserializer:previousValue:)")));
- (int16_t)decodeShortElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeShortElement(descriptor:index:)")));
- (NSString *)decodeStringElementDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor index:(int32_t)index __attribute__((swift_name("decodeStringElement(descriptor:index:)")));
- (void)endStructureDescriptor:(id<SharedKotlinx_serialization_coreSerialDescriptor>)descriptor __attribute__((swift_name("endStructure(descriptor:)")));
@property (readonly) SharedKotlinx_serialization_coreSerializersModule *serializersModule __attribute__((swift_name("serializersModule")));
@end

__attribute__((objc_subclassing_restricted))
__attribute__((swift_name("KotlinNothing")))
@interface SharedKotlinNothing : SharedBase
@end


/**
 * @note annotations
 *   kotlinx.serialization.ExperimentalSerializationApi
*/
__attribute__((swift_name("Kotlinx_serialization_coreSerializersModuleCollector")))
@protocol SharedKotlinx_serialization_coreSerializersModuleCollector
@required
- (void)contextualKClass:(id<SharedKotlinKClass>)kClass provider:(id<SharedKotlinx_serialization_coreKSerializer> (^)(NSArray<id<SharedKotlinx_serialization_coreKSerializer>> *))provider __attribute__((swift_name("contextual(kClass:provider:)")));
- (void)contextualKClass:(id<SharedKotlinKClass>)kClass serializer:(id<SharedKotlinx_serialization_coreKSerializer>)serializer __attribute__((swift_name("contextual(kClass:serializer:)")));
- (void)polymorphicBaseClass:(id<SharedKotlinKClass>)baseClass actualClass:(id<SharedKotlinKClass>)actualClass actualSerializer:(id<SharedKotlinx_serialization_coreKSerializer>)actualSerializer __attribute__((swift_name("polymorphic(baseClass:actualClass:actualSerializer:)")));
- (void)polymorphicDefaultBaseClass:(id<SharedKotlinKClass>)baseClass defaultDeserializerProvider:(id<SharedKotlinx_serialization_coreDeserializationStrategy> _Nullable (^)(NSString * _Nullable))defaultDeserializerProvider __attribute__((swift_name("polymorphicDefault(baseClass:defaultDeserializerProvider:)"))) __attribute__((deprecated("Deprecated in favor of function with more precise name: polymorphicDefaultDeserializer")));
- (void)polymorphicDefaultDeserializerBaseClass:(id<SharedKotlinKClass>)baseClass defaultDeserializerProvider:(id<SharedKotlinx_serialization_coreDeserializationStrategy> _Nullable (^)(NSString * _Nullable))defaultDeserializerProvider __attribute__((swift_name("polymorphicDefaultDeserializer(baseClass:defaultDeserializerProvider:)")));
- (void)polymorphicDefaultSerializerBaseClass:(id<SharedKotlinKClass>)baseClass defaultSerializerProvider:(id<SharedKotlinx_serialization_coreSerializationStrategy> _Nullable (^)(id))defaultSerializerProvider __attribute__((swift_name("polymorphicDefaultSerializer(baseClass:defaultSerializerProvider:)")));
@end

__attribute__((swift_name("KotlinKDeclarationContainer")))
@protocol SharedKotlinKDeclarationContainer
@required
@end

__attribute__((swift_name("KotlinKAnnotatedElement")))
@protocol SharedKotlinKAnnotatedElement
@required
@end


/**
 * @note annotations
 *   kotlin.SinceKotlin(version="1.1")
*/
__attribute__((swift_name("KotlinKClassifier")))
@protocol SharedKotlinKClassifier
@required
@end

__attribute__((swift_name("KotlinKClass")))
@protocol SharedKotlinKClass <SharedKotlinKDeclarationContainer, SharedKotlinKAnnotatedElement, SharedKotlinKClassifier>
@required

/**
 * @note annotations
 *   kotlin.SinceKotlin(version="1.1")
*/
- (BOOL)isInstanceValue:(id _Nullable)value __attribute__((swift_name("isInstance(value:)")));
@property (readonly) NSString * _Nullable qualifiedName __attribute__((swift_name("qualifiedName")));
@property (readonly) NSString * _Nullable simpleName __attribute__((swift_name("simpleName")));
@end

#pragma pop_macro("_Nullable_result")
#pragma clang diagnostic pop
NS_ASSUME_NONNULL_END
